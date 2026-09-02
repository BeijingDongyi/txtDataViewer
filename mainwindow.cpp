#include "mainwindow.h"
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QHeaderView>
#include <QDir>
#include <cmath>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QPainterPath>
#include <algorithm>
#include <QStringList>
#include <QShortcut>
#include <QTimer>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#define TXT_VIEWER_SKIP_EMPTY_PARTS Qt::SkipEmptyParts
#else
#define TXT_VIEWER_SKIP_EMPTY_PARTS QString::SkipEmptyParts
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#include <QRegularExpression>
#else
#include <QRegExp>
#endif

static int plotWindowCounter = 0;

static int textWidth(const QFontMetrics& fm, const QString& text)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return fm.horizontalAdvance(text);
#else
    return fm.width(text);
#endif
}

static const QVector<QColor>& groupColors()
{
    static const QVector<QColor> colors = {
        QColor(255, 255, 255), QColor(255, 230, 200), QColor(255, 230, 255),
        QColor(255, 255, 200), QColor(240, 230, 255), QColor(255, 245, 215), QColor(225, 245, 245)
    };
    return colors;
}

CheckableHeaderView::CheckableHeaderView(QMap<int, int>* colGroupMap, QVector<int>* displayColNums,
                                         Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
    , m_colGroupMap(colGroupMap)
    , m_displayColNums(displayColNums)
{
    setMouseTracking(true);
    setSectionsClickable(true);
    setSectionResizeMode(QHeaderView::Fixed);
}

void CheckableHeaderView::setCheckState(int column, bool checked)
{
    if (column >= 0 && column < m_checkedStates.size()) {
        m_checkedStates[column] = checked;
        updateSection(column);
    }
}

bool CheckableHeaderView::getCheckState(int column) const
{
    return (column >= 0 && column < m_checkedStates.size()) ? m_checkedStates[column] : false;
}

QVector<int> CheckableHeaderView::getCheckedColumns() const
{
    QVector<int> checkedCols;
    for (int i = 0; i < m_checkedStates.size(); ++i)
        if (m_checkedStates[i]) checkedCols.append(i);
    return checkedCols;
}

void CheckableHeaderView::initCheckStates(int columnCount)
{
    m_checkedStates.clear();
    m_checkedStates.resize(columnCount);
    m_checkedStates.fill(false);
    update();
}

void CheckableHeaderView::clearAllCheckStates()
{
    for (int i = 0; i < m_checkedStates.size(); ++i)
        m_checkedStates[i] = false;
    update();
}

void CheckableHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    painter->fillRect(rect, getGroupHeaderBgColor(logicalIndex));
    painter->setPen(QPen(QColor(200, 200, 200), 1));
    painter->drawRect(rect.adjusted(0, 0, -1, -1));

    const int checkBoxSize = 20;
    QRect checkRect(rect.left() + 6, rect.center().y() - checkBoxSize/2, checkBoxSize, checkBoxSize);
    painter->setPen(QPen(QColor(70, 70, 70), 1.5));
    painter->setBrush(QColor(250, 250, 250));
    painter->drawRoundedRect(checkRect, 3, 3);

    if (logicalIndex >= 0 && logicalIndex < m_checkedStates.size() && m_checkedStates[logicalIndex]) {
        QPen tickPen(QColor(33, 150, 243), 2.5);
        tickPen.setCapStyle(Qt::RoundCap);
        tickPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(tickPen);
        painter->drawLines({
            QLineF(checkRect.left()+5, checkRect.center().y(), checkRect.center().x()-2, checkRect.bottom()-5),
            QLineF(checkRect.center().x()-2, checkRect.bottom()-5, checkRect.right()-5, checkRect.top()+5)
        });
    }

    QRect textRect(rect.left() + checkBoxSize + 14, rect.top() + 2, rect.width() - checkBoxSize - 22, rect.height() - 4);
    QString headerText = model()->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
    QFont headerFont = font();
    headerFont.setBold(true);
    headerFont.setPointSize(9);
    painter->setFont(headerFont);
    painter->setPen(QColor(40, 40, 40));
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::ElideRight, headerText);

    painter->restore();
}

void CheckableHeaderView::mousePressEvent(QMouseEvent *event)
{
    int logicalIndex = logicalIndexAt(event->pos());
    if (logicalIndex >= 0 && logicalIndex < m_checkedStates.size()) {
        m_checkedStates[logicalIndex] = !m_checkedStates[logicalIndex];
        updateSection(logicalIndex);
        emit checkStateChanged(logicalIndex, m_checkedStates[logicalIndex]);
        return;
    }
    QHeaderView::mousePressEvent(event);
}

QColor CheckableHeaderView::getGroupHeaderBgColor(int logicalIndex) const
{
    if (!m_displayColNums || !m_colGroupMap || logicalIndex < 0 || logicalIndex >= m_displayColNums->size())
        return QColor(240,240,240);
    
    int colNum = m_displayColNums->value(logicalIndex, 0);
    int groupIdx = m_colGroupMap->value(colNum, 0);
    
    const auto& colors = groupColors();
    return groupIdx < colors.size() ? colors[groupIdx] : QColor(240,240,240);
}

BigDataTableModel::BigDataTableModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_rawData(nullptr)
{
}

void BigDataTableModel::setData(const QVector<QVector<QString>>* data, const QVector<int>& redColStartRows, const QVector<int>& displayColNums)
{
    beginResetModel();
    m_rawData = data;
    m_redColStartRows = redColStartRows;
    m_displayColNums = displayColNums;
    endResetModel();
}

void BigDataTableModel::setColGroupMap(const QMap<int, int>& map) { m_colGroupMap = map; }

QString BigDataTableModel::getData(int row, int col) const
{
    return (m_rawData && row >= 0 && row < m_rawData->size() && col >= 0 && col < (*m_rawData)[row].size()) ? (*m_rawData)[row][col] : "";
}

int BigDataTableModel::rowCount(const QModelIndex &parent) const
{
    return (parent.isValid() || !m_rawData) ? 0 : m_rawData->size();
}

int BigDataTableModel::columnCount(const QModelIndex &parent) const
{
    return (parent.isValid() || !m_rawData || m_rawData->isEmpty()) ? 0 : (*m_rawData)[0].size();
}

QVariant BigDataTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    int row = index.row(), col = index.column();

    if (role == Qt::DisplayRole) return getData(row, col);
    if (role == Qt::ForegroundRole) {
        if (col >= 0 && col < m_redColStartRows.size() && m_redColStartRows[col] != -1 && row >= m_redColStartRows[col])
            return QColor(Qt::red);
        return QColor(Qt::black);
    }
    if (role == Qt::BackgroundRole) {
        int colNum = m_displayColNums.value(col, 0);
        int groupIdx = m_colGroupMap.value(colNum, 0);
        const auto& colors = groupColors();
        return groupIdx < colors.size() ? colors[groupIdx] : QColor(Qt::white);
    }
    return QVariant();
}

QVariant BigDataTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        return (orientation == Qt::Horizontal) ? QString::number(m_displayColNums[section]) : QString::number(section + 1);
    }
    return QVariant();
}

QString BigDataTableModel::getColumnLabel(int column) const
{
    return headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
}

PlotWidget::PlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_mouseInWidget(false)
    , m_isDragging(false)
    , m_zoomEnabled(false)
    , m_originalXMin(0.0), m_originalXMax(10.0)
    , m_originalYMin(0.0), m_originalYMax(1.0)
    , m_currentXMin(0.0), m_currentXMax(10.0)
    , m_currentYMin(0.0), m_currentYMax(1.0)
    , m_hoveredCurveIdx(-1)
{
    setMouseTracking(true);
    setMinimumSize(800, 600);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PlotWidget::setZoomEnabled(bool enabled)
{
    m_zoomEnabled = enabled;
    if (!m_zoomEnabled && m_isDragging) {
        m_isDragging = false;
        m_dragStart = QPoint();
        m_dragEnd = QPoint();
        update();
    }
}

double PlotWidget::processYValue(int curveIdx, double y) const
{
    if (curveIdx < 0 || curveIdx >= m_plotData.size()) return y;
    double scaledY = y * m_plotData[curveIdx].yScale;
    if (m_plotData[curveIdx].yInverted) {
        double yMid = (m_originalYMin + m_originalYMax) / 2;
        scaledY = yMid - (scaledY - yMid);
    }
    return scaledY;
}

void PlotWidget::updatePlotRange()
{
    calculateOriginalRange();
    resetView();
}

void PlotWidget::setCurveYInverted(int curveIdx, bool inverted)
{
    if (curveIdx >= 0 && curveIdx < m_plotData.size()) {
        m_plotData[curveIdx].yInverted = inverted;
        updatePlotRange();
    }
}

void PlotWidget::setCurveYScale(int curveIdx, double scale)
{
    if (curveIdx >= 0 && curveIdx < m_plotData.size() && scale > 0) {
        m_plotData[curveIdx].yScale = scale;
        updatePlotRange();
    }
}

void PlotWidget::setCurveVisible(int curveIdx, bool visible)
{
    if (curveIdx >= 0 && curveIdx < m_plotData.size()) {
        m_plotData[curveIdx].visible = visible;
        m_curveCache = QPixmap();
        m_legendCache = QPixmap();
        update();
        emit curveVisibilityChanged(curveIdx, visible);
    }
}

void PlotWidget::setAllCurvesVisible(bool visible)
{
    for (int i = 0; i < m_plotData.size(); ++i) {
        m_plotData[i].visible = visible;
    }
    m_curveCache = QPixmap();
    m_legendCache = QPixmap();
    update();
}

QStringList PlotWidget::getCurveLabels() const
{
    QStringList labels;
    for (const auto& data : m_plotData) labels.append(data.colLabel);
    return labels;
}

void PlotWidget::setData(const QList<PlotData>& data, bool resetCurveOptions)
{
    m_plotData = data;
    if (resetCurveOptions) {
        for (auto& plotData : m_plotData) {
            plotData.yInverted = false;
            plotData.yScale = 1.0;
            plotData.visible = true;
        }
    }
    calculateOriginalRange();
    resetView();
    m_backgroundCache = QPixmap();
    m_curveCache = QPixmap();
    m_legendCache = QPixmap();
    m_tipTextRects.clear();
    update();
}

void PlotWidget::resetView()
{
    m_currentXMin = m_originalXMin;
    m_currentXMax = m_originalXMax;
    m_currentYMin = m_originalYMin;
    m_currentYMax = m_originalYMax;
    m_isDragging = false;
    m_dragStart = QPoint();
    m_dragEnd = QPoint();
    
    // 新增：清空所有缓存，强制重新绘制坐标轴和曲线
    m_backgroundCache = QPixmap();
    m_curveCache = QPixmap();
    m_legendCache = QPixmap();
    
    m_tipTextRects.clear();
    update();
}


void PlotWidget::calculateOriginalRange()
{
    if (m_plotData.isEmpty()) {
        m_originalXMin = 0.0; m_originalXMax = 10.0;
        m_originalYMin = 0.0; m_originalYMax = 1.0;
        return;
    }

    double xMin = 0.0, xMax = -1e18;
    double yMin = 1e18, yMax = -1e18;

    for (int curveIdx = 0; curveIdx < m_plotData.size(); ++curveIdx) {
        if (!m_plotData[curveIdx].visible) continue;
        const auto& d = m_plotData[curveIdx];
        if (d.yData.isEmpty()) continue;
        xMax = qMax(xMax, static_cast<double>(d.yData.size() - 1));
        for (double v : d.yData) {
            double processedY = processYValue(curveIdx, v);
            yMin = qMin(yMin, processedY);
            yMax = qMax(yMax, processedY);
        }
    }

    if (xMax <= 0) xMax = 10;
    if (yMin >= yMax) { yMin = 0; yMax = 10; }

    double xMargin = (xMax - xMin) * 0.08;
    double yMargin = (yMax - yMin) * 0.08;

    m_originalXMin = xMin;
    m_originalXMax = xMax + xMargin;
    m_originalYMin = yMin - yMargin;
    m_originalYMax = yMax + yMargin;
}

void PlotWidget::getPlotRect(int& marginL, int& marginT, int& marginR, int& marginB, int& plotW, int& plotH)
{
    QFont tickFont("Microsoft YaHei", 9, QFont::Normal);
    QFont labelFont("Microsoft YaHei", 11, QFont::Bold);
    QFontMetrics tickFm(tickFont);
    QFontMetrics labelFm(labelFont);

    marginL = qMax(110, tickFm.horizontalAdvance("-123456789") + labelFm.height() + 36);
    marginT = qMax(50, labelFm.height() + 24);
    marginR = 220;
    marginB = qMax(100, tickFm.height() * 2 + labelFm.height() + 34);
    plotW = qMax(width() - marginL - marginR, 100);
    plotH = qMax(height() - marginT - marginB, 100);
}

void PlotWidget::updateBackgroundCache()
{
    if (size().isEmpty() || (!m_backgroundCache.isNull() && m_backgroundCache.size() == size())) return;

    m_backgroundCache = QPixmap(width(), height());
    m_backgroundCache.fill(QColor(248, 248, 248));
    QPainter p(&m_backgroundCache);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    int marginL, marginT, marginR, marginB, plotW, plotH;
    getPlotRect(marginL, marginT, marginR, marginB, plotW, plotH);
    int w = width(), h = height();
    QRectF plotRect(marginL, marginT, plotW, plotH);

    p.setPen(QColor(220, 220, 220));
    p.setBrush(QColor(255, 255, 255));
    p.drawRect(plotRect);

    QPen axisPen(QColor(40, 40, 40), 1.5, Qt::SolidLine, Qt::SquareCap, Qt::RoundJoin);
    p.setPen(axisPen);
    p.drawLine(marginL, marginT, marginL, h - marginB);
    p.drawLine(marginL, h - marginB, w - marginR, h - marginB);

    QFont labelFont("Microsoft YaHei", 11, QFont::Bold);
    QFont tickFont("Microsoft YaHei", 9, QFont::Normal);
    QFontMetrics labelFm(labelFont);
    QFontMetrics tickFm(tickFont);

    p.setFont(labelFont);
    p.setPen(QColor(30, 30, 30));
    p.save();
    p.translate(labelFm.height() + 12, marginT + plotH / 2.0);
    p.rotate(-90);
    p.drawText(QRectF(-plotH / 2.0, 0, plotH, labelFm.height() + 6), Qt::AlignCenter, "Y轴：数值");
    p.restore();
    p.drawText(QRectF(marginL, h - marginB + tickFm.height() + 20, plotW, labelFm.height() + 8),
               Qt::AlignCenter, "X轴：数据索引");

    p.setFont(tickFont);
    p.setPen(QColor(60, 60, 60));
    const int tickCount = 10;
    double xRange = m_currentXMax - m_currentXMin;
    double yRange = m_currentYMax - m_currentYMin;
    
    // 修复：范围无效时直接返回，不绘制错误刻度
    if (xRange <= 1e-8 || yRange <= 1e-8) {
        return;
    }

    double xTickStep = xRange / tickCount;
    for (int i = 0; i <= tickCount; ++i) {
        double dataX = m_currentXMin + i * xTickStep;
        double pixelX = marginL + (i / (double)tickCount) * plotW;
        p.drawLine(QPointF(pixelX, h - marginB), QPointF(pixelX, h - marginB + 8));
        p.drawText(QRectF(pixelX - 45, h - marginB + 10, 90, tickFm.height() + 6),
                   Qt::AlignCenter, QString::number(dataX, 'g', 6));
    }

    double yTickStep = yRange / tickCount;
    for (int i = 0; i <= tickCount; ++i) {
        double dataY = m_currentYMin + i * yTickStep;
        double pixelY = h - marginB - (i / (double)tickCount) * plotH;
        p.drawLine(QPointF(marginL, pixelY), QPointF(marginL - 8, pixelY));
        p.drawText(QRectF(labelFm.height() + 18, pixelY - tickFm.height() / 2.0,
                          marginL - labelFm.height() - 32, tickFm.height() + 6),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(dataY, 'g', 5));
    }
}

QPolygonF PlotWidget::buildVisiblePolyline(int curveIdx, const PlotData& data, const QRectF& plotRect, double xRange, double yRange) const
{
    QPolygonF poly;
    const int total = data.yData.size();
    if (total < 2 || xRange <= 0.0 || yRange <= 0.0) return poly;

    int first = qMax(0, static_cast<int>(std::floor(m_currentXMin)) - 1);
    int last = qMin(total - 1, static_cast<int>(std::ceil(m_currentXMax)) + 1);
    if (last <= first) return poly;

    auto toPoint = [&](int idx) {
        double x = idx;
        double y = processYValue(curveIdx, data.yData[idx]);
        double tx = (x - m_currentXMin) / xRange;
        double ty = (y - m_currentYMin) / yRange;
        return QPointF(plotRect.left() + tx * plotRect.width(),
                       plotRect.bottom() - ty * plotRect.height());
    };

    int visibleCount = last - first + 1;
    int maxPoints = qMax(1000, static_cast<int>(plotRect.width()) * 3);
    if (visibleCount <= maxPoints) {
        poly.reserve(visibleCount);
        for (int i = first; i <= last; ++i) poly << toPoint(i);
        return poly;
    }

    int bucketCount = qMax(1, static_cast<int>(plotRect.width()));
    poly.reserve(bucketCount * 2 + 2);
    poly << toPoint(first);

    double pointsPerBucket = visibleCount / static_cast<double>(bucketCount);
    for (int bucket = 0; bucket < bucketCount; ++bucket) {
        int bucketStart = first + static_cast<int>(bucket * pointsPerBucket);
        int bucketEnd = first + static_cast<int>((bucket + 1) * pointsPerBucket) - 1;
        bucketStart = qBound(first, bucketStart, last);
        bucketEnd = qBound(bucketStart, bucketEnd, last);

        int minIdx = bucketStart;
        int maxIdx = bucketStart;
        double minY = processYValue(curveIdx, data.yData[bucketStart]);
        double maxY = minY;
        for (int i = bucketStart + 1; i <= bucketEnd; ++i) {
            double y = processYValue(curveIdx, data.yData[i]);
            if (y < minY) { minY = y; minIdx = i; }
            if (y > maxY) { maxY = y; maxIdx = i; }
        }

        if (minIdx < maxIdx) {
            poly << toPoint(minIdx) << toPoint(maxIdx);
        } else if (maxIdx < minIdx) {
            poly << toPoint(maxIdx) << toPoint(minIdx);
        } else {
            poly << toPoint(minIdx);
        }
    }

    poly << toPoint(last);
    return poly;
}

void PlotWidget::updateCurveCache()
{
    if (size().isEmpty()) return;
    if (!m_curveCache.isNull() && m_curveCache.size() == size()) return;

    int marginL, marginT, marginR, marginB, plotW, plotH;
    getPlotRect(marginL, marginT, marginR, marginB, plotW, plotH);
    
    m_curveCache = QPixmap(width(), height());
    m_curveCache.fill(Qt::transparent);
    QPainter p(&m_curveCache);
    p.setRenderHints(QPainter::Antialiasing);

    double xRange = m_currentXMax - m_currentXMin;
    double yRange = m_currentYMax - m_currentYMin;
    if (xRange <= 0 || yRange <= 0) return;

    int curveIdx = 0;
    QRectF plotRect(marginL, marginT, plotW, plotH);

    for (int i = 0; i < m_plotData.size(); ++i) {
        if (!m_plotData[i].visible) continue;
        const auto& d = m_plotData[i];
        if (d.yData.size() < 2) continue;

        QColor lineColor = m_curveColors[curveIdx % m_curveColors.size()];
        curveIdx++;
        p.setPen(QPen(lineColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);

        QPolygonF poly = buildVisiblePolyline(i, d, plotRect, xRange, yRange);
        if (!poly.isEmpty()) p.drawPolyline(poly);
    }
}

void PlotWidget::updateLegendCache()
{
    if (size().isEmpty()) return;
    if (!m_legendCache.isNull() && m_legendCache.size() == size()) return;

    int marginL, marginT, marginR, marginB, plotW, plotH;
    getPlotRect(marginL, marginT, marginR, marginB, plotW, plotH);
    int w = width();

    m_legendCache = QPixmap(width(), height());
    m_legendCache.fill(Qt::transparent);
    QPainter p(&m_legendCache);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    int curveIdx = 0;
    int legendY = marginT + 20;
    const int legendItemHeight = 25;
    const int checkBoxSize = 16;
    p.setFont(QFont("Microsoft YaHei", 10, QFont::Normal));
    m_tipTextRects.clear();
    m_checkBoxRects.clear();
    
    for (const auto& d : m_plotData) {
        if (d.yData.isEmpty()) continue;
        QColor c = m_curveColors[curveIdx % m_curveColors.size()];
        curveIdx++;
        int legendX = w - marginR + 20;

        QRectF checkRect(legendX, legendY + (25 - checkBoxSize)/2, checkBoxSize, checkBoxSize);
        m_checkBoxRects.append(checkRect);
        
        p.setPen(QPen(QColor(70, 70, 70), 1.5));
        p.setBrush(d.visible ? QColor(33, 150, 243) : QColor(250, 250, 250));
        p.drawRoundedRect(checkRect, 3, 3);

        if (d.visible) {
            QPen tickPen(Qt::white, 2);
            tickPen.setCapStyle(Qt::RoundCap);
            tickPen.setJoinStyle(Qt::RoundJoin);
            p.setPen(tickPen);
            p.drawLines({
                QLineF(checkRect.left()+3, checkRect.center().y(), checkRect.center().x()-1, checkRect.bottom()-3),
                QLineF(checkRect.center().x()-1, checkRect.bottom()-3, checkRect.right()-3, checkRect.top()+3)
            });
        }

        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(legendX + checkBoxSize + 6, legendY + 6, 12, 12);

        QString invertTag = d.yInverted ? "[已反转]" : "[点击反转]";
        QString scaleTag = QString("(缩放×%1)").arg(d.yScale, 0, 'g', 3);
        QString legendText = QString("%1 %2 %3").arg(d.colLabel).arg(invertTag).arg(scaleTag);
        p.setPen(QColor(30, 30, 30));
        QRectF textRect(legendX + checkBoxSize + 6 + 12 + 6, legendY - 5, 150, 25);
        p.drawText(textRect, Qt::AlignVCenter, legendText);
        m_tipTextRects.append(textRect);
        legendY += legendItemHeight;
    }
}

double PlotWidget::interpolateY(const QVector<double>& yData, double targetX)
{
    if (yData.isEmpty()) return 0.0;
    if (targetX <= 0.0) return yData.first();
    if (targetX >= yData.size() - 1) return yData.last();

    int i = qBound(1, static_cast<int>(std::ceil(targetX)), yData.size() - 1);
    double x1 = i - 1;
    double x2 = i;
    double y1 = yData[i-1], y2 = yData[i];
    return y1 + (y2 - y1) * (targetX - x1) / (x2 - x1);
}

void PlotWidget::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    m_backgroundCache = QPixmap();
    m_curveCache = QPixmap();
    m_legendCache = QPixmap();
    m_tipTextRects.clear();
    m_checkBoxRects.clear();
    update();
}

void PlotWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_hoveredCurveIdx = -1;
        
        for (int i = 0; i < m_checkBoxRects.size(); ++i) {
            if (m_checkBoxRects[i].contains(event->pos())) {
                setCurveVisible(i, !m_plotData[i].visible);
                return;
            }
        }

        for (int i = 0; i < m_tipTextRects.size(); ++i) {
            if (m_tipTextRects[i].contains(event->pos())) {
                m_hoveredCurveIdx = i;
                emit curveInvertClicked(i);
                return;
            }
        }

        int marginL, marginT, marginR, marginB, plotW, plotH;
        getPlotRect(marginL, marginT, marginR, marginB, plotW, plotH);
        QRect plotRect(marginL, marginT, plotW, plotH);
        if (m_zoomEnabled && plotRect.contains(event->pos())) {
            m_dragStart = event->pos();
            m_dragEnd = m_dragStart;
            m_isDragging = true;
        }
    }
    QWidget::mousePressEvent(event);
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    QPoint oldMousePos = m_mousePos;
    m_mousePos = event->pos();
    m_mouseInWidget = rect().contains(m_mousePos);

    int hoveredIdx = -1;
    for (int i = 0; i < m_tipTextRects.size(); ++i) {
        if (m_tipTextRects[i].contains(m_mousePos)) {
            hoveredIdx = i;
            break;
        }
    }
    m_hoveredCurveIdx = hoveredIdx;

    if (m_isDragging && event->buttons() & Qt::LeftButton)
    {
        int marginL, marginT, marginR, marginB, plotW, plotH;
        getPlotRect(marginL, marginT, marginR, marginB, plotW, plotH);
        QRect plotRect(marginL, marginT, plotW, plotH);

        // 【核心修复】把鼠标位置限制在 plotRect 内部
        QPoint boundedPos = event->pos();
        boundedPos.setX(qBound(plotRect.left(), boundedPos.x(), plotRect.right()));
        boundedPos.setY(qBound(plotRect.top(), boundedPos.y(), plotRect.bottom()));
        m_dragEnd = boundedPos;

        update(plotRect);
    }
    else if (m_mouseInWidget || rect().contains(oldMousePos))
    {
        int marginL, marginT, marginR, marginB, plotW, plotH;
        getPlotRect(marginL, marginT, marginR, marginB, plotW, plotH);
        update(QRect(marginL - 80, marginT, width() - marginL - marginR + 300, height() - marginT - marginB));
    }

    QWidget::mouseMoveEvent(event);
}


void PlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDragging)
    {
        m_isDragging = false;
        int marginL, marginT, marginR, marginB, plotW, plotH;
        getPlotRect(marginL, marginT, marginR, marginB, plotW, plotH);
        QRect plotRect(marginL, marginT, plotW, plotH);

        // 【核心修复】终点强制限制在绘布内
        QPoint dragEnd = event->pos();
        dragEnd.setX(qBound(plotRect.left(), dragEnd.x(), plotRect.right()));
        dragEnd.setY(qBound(plotRect.top(), dragEnd.y(), plotRect.bottom()));

        QRect selectedRect(m_dragStart, dragEnd);
        selectedRect = selectedRect.normalized().intersected(plotRect);
        if (selectedRect.width() < 5 || selectedRect.height() < 5) {
            m_dragStart = QPoint();
            m_dragEnd = QPoint();
            update();
            return;
        }

        QPoint start = selectedRect.topLeft();
        QPoint end = selectedRect.bottomRight();

        double xRange = m_currentXMax - m_currentXMin;
        double yRange = m_currentYMax - m_currentYMin;

        double newXMin = m_currentXMin + ((start.x() - marginL) / (double)plotW) * xRange;
        double newXMax = m_currentXMin + ((end.x() - marginL) / (double)plotW) * xRange;
        double newYMax = m_currentYMin + ((plotH - (start.y() - marginT)) / (double)plotH) * yRange;
        double newYMin = m_currentYMin + ((plotH - (end.y() - marginT)) / (double)plotH) * yRange;

        m_currentXMin = qMin(newXMin, newXMax);
        m_currentXMax = qMax(newXMin, newXMax);
        m_currentYMin = qMin(newYMin, newYMax);
        m_currentYMax = qMax(newYMin, newYMax);

        double minRange = 0.001;
        if (m_currentXMax - m_currentXMin < minRange)
        {
            double midX = (m_currentXMax + m_currentXMin) / 2;
            m_currentXMin = midX - minRange/2;
            m_currentXMax = midX + minRange/2;
        }
        if (m_currentYMax - m_currentYMin < minRange)
        {
            double midY = (m_currentYMax + m_currentYMin) / 2;
            m_currentYMin = midY - minRange/2;
            m_currentYMax = midY + minRange/2;
        }

        m_dragStart = QPoint();
        m_dragEnd = QPoint();
        m_backgroundCache = QPixmap();
        m_curveCache = QPixmap();
        update();
    }

    QWidget::mouseReleaseEvent(event);
}


void PlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);

    if (m_backgroundCache.isNull()) updateBackgroundCache();
    p.drawPixmap(0, 0, m_backgroundCache);

    if (m_curveCache.isNull()) updateCurveCache();
    p.drawPixmap(0, 0, m_curveCache);

    if (m_legendCache.isNull()) updateLegendCache();
    p.drawPixmap(0, 0, m_legendCache);

    int marginL, marginT, marginR, marginB, plotW, plotH;
    getPlotRect(marginL, marginT, marginR, marginB, plotW, plotH);
    QRectF plotRect(marginL, marginT, plotW, plotH);
    int w = width(), h = height();

    if (m_mouseInWidget && plotRect.contains(m_mousePos) && !m_isDragging) {
        double xRange = m_currentXMax - m_currentXMin;
        double yRange = m_currentYMax - m_currentYMin;
        if (xRange <= 0 || yRange <= 0) return;

        p.setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);
        p.setPen(QPen(QColor(255, 110, 0), 1.5, Qt::DashLine));
        p.drawLine(QPointF(m_mousePos.x(), marginT), QPointF(m_mousePos.x(), h - marginB));

        double targetX = m_currentXMin + ((m_mousePos.x() - marginL) / (double)plotW) * xRange;
        QFont tipFont("Microsoft YaHei", 10, QFont::Bold);
        tipFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(tipFont);

        int colorIdx = 0;
        for (int curveIdx = 0; curveIdx < m_plotData.size(); ++curveIdx) {
            const auto& d = m_plotData[curveIdx];
            if (!d.visible) continue;
            if (d.yData.isEmpty()) continue;
            QColor curveColor = m_curveColors[colorIdx % m_curveColors.size()];
            colorIdx++;

            double clampedX = qBound(0.0, targetX, static_cast<double>(d.yData.size() - 1));
            double rawY = interpolateY(d.yData, clampedX);
            double realY = processYValue(curveIdx, rawY);
            double realX = targetX;

            double tx = (realX - m_currentXMin) / xRange;
            double ty = (realY - m_currentYMin) / yRange;
            double px = marginL + tx * plotW;
            double py = h - marginB - ty * plotH;

            p.setPen(QPen(curveColor, 2));
            p.setBrush(curveColor);
            p.drawEllipse(QPointF(px, py), 3, 3);

            QString txt = QString("%1  X:%2  原始Y:%3  显示Y:%4")
                .arg(d.colLabel)
                .arg(QString::number(realX, 'g', 6))
                .arg(QString::number(rawY, 'g', 6))
                .arg(QString::number(realY, 'g', 6));

            QPointF textPos(px + 12, py + 4);
            QPainterPath path;
            path.addText(textPos, tipFont, txt);
            QPen strokePen(curveColor, 0.8);
            strokePen.setJoinStyle(Qt::RoundJoin);
            p.setPen(strokePen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::black);
            p.drawPath(path);
        }
    }

    if (m_hoveredCurveIdx >= 0 && m_hoveredCurveIdx < m_tipTextRects.size()) {
        p.setPen(QPen(QColor(33, 150, 243), 1, Qt::SolidLine));
        p.setBrush(QColor(33, 150, 243, 30));
        p.drawRoundedRect(m_tipTextRects[m_hoveredCurveIdx], 4, 4);
    }

    if (m_isDragging) {
        QRect dragRect(QPoint(std::min(m_dragStart.x(), m_dragEnd.x()), std::min(m_dragStart.y(), m_dragEnd.y())),
                       QPoint(std::max(m_dragStart.x(), m_dragEnd.x()), std::max(m_dragStart.y(), m_dragEnd.y())));
        p.setPen(QPen(QColor(33, 150, 243), 2, Qt::DashLine));
        p.setBrush(QColor(33, 150, 243, 50));
        p.drawRect(dragRect.intersected(plotRect.toRect()));
    }
}

PlotDialog::PlotDialog(QWidget *parent, const QList<PlotData>& plotData,
                       const QVector<QVector<double>>* sourceNumericData,
                       const QVector<QVector<uchar>>* sourceNumericValidData,
                       const QVector<int>* sourceCols,
                       const QHash<int, int>* sourceColIndexMap,
                       const QMap<int, int>* colGroupMap,
                       int startRow, int endRow)
    : QDialog(parent)
    , m_zoomMode(false)
    , m_sourceNumericData(sourceNumericData)
    , m_sourceNumericValidData(sourceNumericValidData)
    , m_sourceCols(sourceCols)
    , m_sourceColIndexMap(sourceColIndexMap)
    , m_colGroupMap(colGroupMap)
    , m_startRow(startRow)
    , m_endRow(endRow)
{
    plotWindowCounter++;
    setWindowTitle(QString("画布%1").arg(plotWindowCounter));
    setMinimumSize(1400, 800);
    setSizeGripEnabled(true);
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    setModal(false);

    m_zoomMode = true;

    m_zoomBtn = new QPushButton("取消放大", this);
    m_resetBtn = new QPushButton("还原视图", this);
    m_hideAllBtn = new QPushButton("隐藏全部", this);
    m_showAllBtn = new QPushButton("显示全部", this);
    m_shiftLeftBtn = new QPushButton("←", this);
    m_shiftRightBtn = new QPushButton("→", this);
    m_scaleLabel = new QLabel("Y轴缩放：", this);
    m_curveCombo = new QComboBox(this);
    m_scaleEdit = new QLineEdit("1.0", this);
    m_applyScaleBtn = new QPushButton("应用缩放", this);
    m_scaleEdit->setValidator(new QDoubleValidator(0.0001, 10000.0, 6, this));
    m_scaleEdit->setFixedWidth(80);

    QString btnStyle = R"(
        QPushButton {
            font: 10pt "Microsoft YaHei";
            background-color: #337ab7;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 6px 12px;
            margin: 0 4px;
        }
        QPushButton:hover { background-color: #286090; }
    )";
    m_zoomBtn->setStyleSheet(btnStyle);
    m_resetBtn->setStyleSheet(btnStyle);
    m_hideAllBtn->setStyleSheet(btnStyle);
    m_showAllBtn->setStyleSheet(btnStyle);
    m_shiftLeftBtn->setStyleSheet(btnStyle);
    m_shiftRightBtn->setStyleSheet(btnStyle);
    m_shiftLeftBtn->setToolTip("所有当前曲线列号 -1 后重绘");
    m_shiftRightBtn->setToolTip("所有当前曲线列号 +1 后重绘");
    m_zoomBtn->setToolTip("快捷键 Z：开启/关闭框选放大");
    m_shiftLeftBtn->setFixedWidth(48);
    m_shiftRightBtn->setFixedWidth(48);
    m_applyScaleBtn->setStyleSheet(btnStyle);
    m_scaleLabel->setStyleSheet("font: 10pt 'Microsoft YaHei'; margin: 0 4px;");
    m_curveCombo->setStyleSheet("font: 10pt 'Microsoft YaHei'; padding: 4px; margin: 0 4px; min-width: 120px;");
    m_scaleEdit->setStyleSheet("font: 10pt 'Microsoft YaHei'; padding: 4px; margin: 0 4px;");

    connect(m_zoomBtn, &QPushButton::clicked, this, &PlotDialog::onZoomInClicked);
    connect(m_resetBtn, &QPushButton::clicked, this, &PlotDialog::onResetClicked);
    connect(m_hideAllBtn, &QPushButton::clicked, this, &PlotDialog::onHideAllClicked);
    connect(m_showAllBtn, &QPushButton::clicked, this, &PlotDialog::onShowAllClicked);
    connect(m_shiftLeftBtn, &QPushButton::clicked, this, &PlotDialog::onShiftLeftClicked);
    connect(m_shiftRightBtn, &QPushButton::clicked, this, &PlotDialog::onShiftRightClicked);
    connect(m_applyScaleBtn, &QPushButton::clicked, this, &PlotDialog::onApplyScaleClicked);
    connect(m_curveCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), 
            this, [this](int index) {
        if (index >= 0 && index < m_plotWidget->m_plotData.size()) {
            m_scaleEdit->setText(QString::number(m_plotWidget->m_plotData[index].yScale, 'g', 3));
        }
    });

    m_plotWidget = new PlotWidget;
    m_plotWidget->setData(plotData);
    m_curveCombo->addItems(m_plotWidget->getCurveLabels());
    if (!m_plotWidget->m_plotData.isEmpty()) {
        m_scaleEdit->setText(QString::number(m_plotWidget->m_plotData[0].yScale, 'g', 3));
    }
    connect(m_plotWidget, &PlotWidget::curveInvertClicked, this, &PlotDialog::onCurveInvertClicked);
    m_plotWidget->setZoomEnabled(m_zoomMode);
    setCursor(Qt::CrossCursor);

    QHBoxLayout* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_zoomBtn);
    btnLayout->addWidget(m_resetBtn);
    btnLayout->addWidget(m_hideAllBtn);
    btnLayout->addWidget(m_showAllBtn);
    btnLayout->addWidget(m_shiftLeftBtn);
    btnLayout->addWidget(m_shiftRightBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_scaleLabel);
    btnLayout->addWidget(m_curveCombo);
    btnLayout->addWidget(m_scaleEdit);
    btnLayout->addWidget(m_applyScaleBtn);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(m_plotWidget, 1);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    m_plotWidget->setFocusPolicy(Qt::StrongFocus);
    m_zoomBtn->setFocusPolicy(Qt::NoFocus);
    m_resetBtn->setFocusPolicy(Qt::NoFocus);
    m_hideAllBtn->setFocusPolicy(Qt::NoFocus);
    m_showAllBtn->setFocusPolicy(Qt::NoFocus);
    m_shiftLeftBtn->setFocusPolicy(Qt::NoFocus);
    m_shiftRightBtn->setFocusPolicy(Qt::NoFocus);
    m_applyScaleBtn->setFocusPolicy(Qt::NoFocus);
    setFocusProxy(m_plotWidget);

    QShortcut* zoomShortcut = new QShortcut(QKeySequence(Qt::Key_Z), this);
    zoomShortcut->setContext(Qt::WindowShortcut);
    connect(zoomShortcut, &QShortcut::activated, this, &PlotDialog::onZoomInClicked);

    QShortcut* closeReturnShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    closeReturnShortcut->setContext(Qt::WindowShortcut);
    connect(closeReturnShortcut, &QShortcut::activated, this, &QDialog::close);

    QShortcut* closeEnterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    closeEnterShortcut->setContext(Qt::WindowShortcut);
    connect(closeEnterShortcut, &QShortcut::activated, this, &QDialog::close);
}

PlotDialog::~PlotDialog() {}

void PlotDialog::onZoomInClicked()
{
    m_zoomMode = !m_zoomMode;
    if (m_zoomMode) {
        m_zoomBtn->setText("取消放大");
        setCursor(Qt::CrossCursor);
    } else {
        m_zoomBtn->setText("放大（框选区域）");
        setCursor(Qt::ArrowCursor);
    }
    m_plotWidget->setZoomEnabled(m_zoomMode);
}

void PlotDialog::onResetClicked()
{
    m_zoomMode = false;
    m_zoomBtn->setText("放大（框选区域）");
    setCursor(Qt::ArrowCursor);
    m_plotWidget->setZoomEnabled(false);
    m_plotWidget->resetView();
}

void PlotDialog::onHideAllClicked()
{
    m_plotWidget->setAllCurvesVisible(false);
}

void PlotDialog::onShowAllClicked()
{
    m_plotWidget->setAllCurvesVisible(true);
}

void PlotDialog::onShiftLeftClicked()
{
    shiftCurves(-1);
}

void PlotDialog::onShiftRightClicked()
{
    shiftCurves(1);
}

void PlotDialog::shiftCurves(int delta)
{
    QList<PlotData> shiftedData;
    QString errorMessage;
    if (!buildShiftedPlotData(delta, shiftedData, errorMessage)) {
        QMessageBox::information(this, "提示", errorMessage);
        return;
    }

    int currentCurveIdx = m_curveCombo->currentIndex();
    m_plotWidget->setData(shiftedData, false);
    m_curveCombo->clear();
    m_curveCombo->addItems(m_plotWidget->getCurveLabels());
    if (!m_plotWidget->m_plotData.isEmpty()) {
        currentCurveIdx = qBound(0, currentCurveIdx, m_plotWidget->m_plotData.size() - 1);
        m_curveCombo->setCurrentIndex(currentCurveIdx);
        m_scaleEdit->setText(QString::number(m_plotWidget->m_plotData[currentCurveIdx].yScale, 'g', 3));
    }
}

bool PlotDialog::buildShiftedPlotData(int delta, QList<PlotData>& shiftedData, QString& errorMessage) const
{
    if (!m_sourceNumericData || !m_sourceNumericValidData || !m_sourceCols || !m_sourceColIndexMap ||
        m_sourceNumericData->isEmpty() || m_sourceNumericValidData->isEmpty() || m_sourceCols->isEmpty()) {
        errorMessage = "当前画布缺少原始数据，无法切换列";
        return false;
    }

    int rowCount = m_sourceNumericData->size();
    int start = qBound(0, m_startRow, rowCount - 1);
    int end = m_endRow < 0 ? rowCount - 1 : qBound(0, m_endRow, rowCount - 1);
    if (start > end) std::swap(start, end);

    shiftedData.reserve(m_plotWidget->m_plotData.size());
    for (const PlotData& oldData : m_plotWidget->m_plotData) {
        int newRawColNum = oldData.rawColNum + delta;
        int newColIndex = m_sourceColIndexMap->value(newRawColNum, -1);
        if (newColIndex < 0) {
            errorMessage = QString("目标列 %1 不在当前加载范围内，已保持原图").arg(newRawColNum);
            return false;
        }

        PlotData newData = oldData;
        newData.colIndex = newColIndex;
        newData.rawColNum = newRawColNum;
        newData.colLabel = QString::number(newRawColNum);
        newData.groupIndex = m_colGroupMap ? m_colGroupMap->value(newRawColNum, oldData.groupIndex) : oldData.groupIndex;
        newData.yData.clear();
        newData.yData.reserve(end - start + 1);

        for (int r = start; r <= end; ++r) {
            double y = 0.0;
            if (newColIndex < (*m_sourceNumericData)[r].size() &&
                r < m_sourceNumericValidData->size() &&
                newColIndex < (*m_sourceNumericValidData)[r].size() &&
                (*m_sourceNumericValidData)[r][newColIndex]) {
                y = (*m_sourceNumericData)[r][newColIndex];
            }
            newData.yData << y;
        }

        shiftedData << newData;
    }

    return !shiftedData.isEmpty();
}

void PlotDialog::onApplyScaleClicked()
{
    int curveIdx = m_curveCombo->currentIndex();
    bool ok;
    double scale = m_scaleEdit->text().toDouble(&ok);
    if (!ok || scale <= 0) {
        QMessageBox::warning(this, "输入错误", "请输入有效的正数（如0.01、1、10等）");
        return;
    }
    m_plotWidget->setCurveYScale(curveIdx, scale);
    m_curveCombo->clear();
    m_curveCombo->addItems(m_plotWidget->getCurveLabels());
    m_curveCombo->setCurrentIndex(curveIdx);
}

void PlotDialog::onCurveInvertClicked(int curveIdx)
{
    if (curveIdx < 0 || curveIdx >= m_plotWidget->getCurveLabels().size()) return;
    bool currentState = m_plotWidget->m_plotData[curveIdx].yInverted;
    m_plotWidget->setCurveYInverted(curveIdx, !currentState);
    m_curveCombo->clear();
    m_curveCombo->addItems(m_plotWidget->getCurveLabels());
    m_curveCombo->setCurrentIndex(curveIdx);
    m_plotWidget->update();
}

int MainWindow::getColumnIndexByRawNum(int rawColNum) const
{
    return m_colIndexByRawNum.value(rawColNum, -1);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_currentPlotDialog(nullptr)
{
    QList<QPair<int, int>> colRanges = {{1,294}, {295,315}, {316,336}, {337,357}, {358,378}, {379,399}, {400,420}};
    QVector<int> selectedCols;
    m_colToGroupIndex.clear();
    for (int groupIdx = 0; groupIdx < colRanges.size(); ++groupIdx) {
        int start = colRanges[groupIdx].first;
        int end = colRanges[groupIdx].second;
        for (int col = start; col <= end; ++col) {
            selectedCols.append(col);
            m_colToGroupIndex[col] = groupIdx;
        }
    }
    m_selectedReadCols = selectedCols;
    m_colIndexByRawNum.clear();
    m_colIndexByRawNum.reserve(m_selectedReadCols.size());
    for (int i = 0; i < m_selectedReadCols.size(); ++i) {
        m_colIndexByRawNum.insert(m_selectedReadCols[i], i);
    }
    m_colBindMap.clear();
    const int BIND_OFFSET = 21;
    for (int col = 357; col <= 389; ++col) {
        int boundCol = col + BIND_OFFSET;
        if (m_selectedReadCols.contains(col) && m_selectedReadCols.contains(boundCol)) {
            m_colBindMap[col] = boundCol;
            m_colBindMap[boundCol] = col;
        }
    }

    initUI();

    QShortcut* enterShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    enterShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enterShortcut, &QShortcut::activated, this, [this]() {
        if (m_currentPlotDialog) {
            closeCurrentPlotDialog();
        } else {
            plotSelectedCols();
        }
    });

    QShortcut* keypadEnterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    keypadEnterShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(keypadEnterShortcut, &QShortcut::activated, this, [this]() {
        if (m_currentPlotDialog) {
            closeCurrentPlotDialog();
        } else {
            plotSelectedCols();
        }
    });

    QShortcut* closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    closeShortcut->setContext(Qt::ApplicationShortcut);
    connect(closeShortcut, &QShortcut::activated, this, &MainWindow::closeCurrentPlotDialog);
}

void MainWindow::initUI()
{
    setWindowTitle("大数据量TXT可视化工具");
    setMinimumSize(1400, 900);

    m_tableModel = new BigDataTableModel(this);
    m_tableModel->setColGroupMap(m_colToGroupIndex);
    m_tableView = new QTableView(this);
    m_tableView->setModel(m_tableModel);

    m_checkableHeader = new CheckableHeaderView(&m_colToGroupIndex, &m_selectedReadCols, Qt::Horizontal, m_tableView);
    m_tableView->setHorizontalHeader(m_checkableHeader);

    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tableView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tableView->setShowGrid(false);
    m_tableView->setFocusPolicy(Qt::NoFocus);
    m_tableView->verticalHeader()->setDefaultSectionSize(28);
    m_tableView->setTextElideMode(Qt::ElideNone);

    QString scrollBarStyle = R"(
        QTableView QScrollBar:horizontal {
            height: 40px;
            background: #f0f0f0;
            margin: 0px;
        }
        QTableView QScrollBar::handle:horizontal {
            background: #337ab7;
            min-width: 80px;
            border-radius: 8px;
        }
        QTableView QScrollBar::handle:horizontal:hover { background: #286090; }
        QTableView QScrollBar::add-line:horizontal, QTableView QScrollBar::sub-line:horizontal {
            height: 0px;
            width: 0px;
        }
        QTableView QScrollBar:vertical {
            width: 40px;
            background: #f0f0f0;
            margin: 0px;
        }
        QTableView QScrollBar::handle:vertical {
            background: #337ab7;
            min-height: 80px;
            border-radius: 8px;
        }
        QTableView QScrollBar::handle:vertical:hover { background: #286090; }
        QTableView QScrollBar::add-line:vertical, QTableView QScrollBar::sub-line:vertical {
            height: 0px;
            width: 0px;
        }
    )";
    m_tableView->setStyleSheet(scrollBarStyle);

    m_loadBtn = new QPushButton("加载TXT数据", this);
    m_plotBtn = new QPushButton("绘制选中列", this);
    m_multiPlotBtn = new QPushButton("多画布分组绘制", this);
    m_jumpCheckBox = new QCheckBox("连选跳转", this);
    m_jumpSpinBox = new QSpinBox(this);
    m_jumpSpinBox->setEnabled(false);
    m_jumpSpinBox->setRange(1, 99999);
    m_jumpSpinBox->setValue(1);
    m_jumpSpinBox->setStyleSheet(R"(
        QSpinBox {
            font: 12pt Monospace;
            padding: 6px 8px;
            border: 1px solid #ccc;
            border-radius: 6px;
            min-width: 80px;
        }
    )");
    m_jumpCheckBox->setStyleSheet("font: 12pt Monospace;");
    connect(m_jumpCheckBox, &QCheckBox::toggled, m_jumpSpinBox, &QSpinBox::setEnabled);
    m_startRowSpin = new QSpinBox(this);
    QLabel* lbl = new QLabel("起始行：", this);
    
    QLabel* endLbl = new QLabel("终止行：", this);
    m_endRowSpin = new QSpinBox(this);
    m_endRowSpin->setStyleSheet(R"(
        QSpinBox {
            font: 12pt Monospace;
            padding: 6px 8px;
            border: 1px solid #ccc;
            border-radius: 6px;
            min-width: 100px;
        }
    )");
    m_endRowSpin->setEnabled(false);

    m_colInputLbl = new QLabel("绘制列（逗号/空格分隔）：", this);
    m_colInputEdit = new QLineEdit(this);
    m_colInputEdit->setPlaceholderText("示例：1,2,3 或 1 2 3");
    m_colInputEdit->setStyleSheet(R"(
        QLineEdit {
            font: 12pt Monospace;
            padding: 6px 8px;
            border: 1px solid #ccc;
            border-radius: 6px;
            min-width: 200px;
        }
    )");
    m_colInputLbl->setStyleSheet("font: 12pt Monospace;");

    QString btnStyle = R"(
        QPushButton {
            font: 12pt "Monospace";
            background-color: #337ab7;
            color: white;
            border: none;
            border-radius: 6px;
            padding: 8px 14px;
            margin: 0 4px;
        }
        QPushButton:hover { background-color: #286090; }
        QPushButton:disabled { background-color: #9accef; }
    )";
    m_loadBtn->setStyleSheet(btnStyle);
    m_plotBtn->setStyleSheet(btnStyle);
    m_multiPlotBtn->setStyleSheet(btnStyle);
    m_plotBtn->setEnabled(false);
    m_multiPlotBtn->setEnabled(false);

    lbl->setStyleSheet("font: 12pt Monospace;");
    m_startRowSpin->setStyleSheet(R"(
        QSpinBox {
            font: 12pt Monospace;
            padding: 6px 8px;
            border: 1px solid #ccc;
            border-radius: 6px;
            min-width: 100px;
        }
    )");
    m_startRowSpin->setEnabled(false);

    QWidget* cw = new QWidget(this);
    QVBoxLayout* mainLay = new QVBoxLayout(cw);
    QHBoxLayout* topLay = new QHBoxLayout;

    topLay->addWidget(m_loadBtn);
    topLay->addWidget(m_plotBtn);
    topLay->addWidget(m_multiPlotBtn);
    topLay->addStretch();
    topLay->addWidget(m_jumpCheckBox);
    topLay->addWidget(m_jumpSpinBox);
    topLay->addWidget(m_colInputLbl);
    topLay->addWidget(m_colInputEdit);
    topLay->addSpacing(20);
    topLay->addWidget(lbl);
    topLay->addWidget(m_startRowSpin);
    topLay->addWidget(endLbl);
    topLay->addWidget(m_endRowSpin);

    mainLay->addLayout(topLay);
    mainLay->addWidget(m_tableView, 1);

    setCentralWidget(cw);

    connect(m_loadBtn, &QPushButton::clicked, this, &MainWindow::loadTxt);
    connect(m_plotBtn, &QPushButton::clicked, this, &MainWindow::plotSelectedCols);
    connect(m_multiPlotBtn, &QPushButton::clicked, this, &MainWindow::plotMultiCanvasByCode);
    connect(m_colInputEdit, &QLineEdit::editingFinished, this, &MainWindow::onColInputEditingFinished);
    connect(m_colInputEdit, &QLineEdit::returnPressed, this, [this]() {
        QTimer::singleShot(0, this, &MainWindow::plotSelectedCols);
    });
    connect(m_checkableHeader, &CheckableHeaderView::checkStateChanged, this, &MainWindow::onHeaderCheckStateChanged);
}

void MainWindow::closeCurrentPlotDialog()
{
    if (m_currentPlotDialog) {
        m_currentPlotDialog->close();
        m_currentPlotDialog = nullptr;
    }
}

bool MainWindow::isNumber(const QString& str) const
{
    bool ok;
    str.toDouble(&ok);
    return ok;
}

QVector<int> MainWindow::calculateColMaxWidth() const
{
    if (m_rawData.isEmpty()) return {};
    int cols = m_selectedReadCols.size();
    QVector<int> res(cols, 220);
    QFont headerFont;
    headerFont.setBold(true);
    headerFont.setPointSize(9);
    QFontMetrics fm(headerFont);
    int maxSample = qMin(static_cast<int>(m_rawData.size()), 80);

    for (int r = 0; r < maxSample; ++r) {
        const auto& row = m_rawData[r];
        for (int c = 0; c < cols && c < row.size(); ++c) {
            int w = textWidth(fm, row[c]) + 90;
            if (w > res[c]) res[c] = w;
        }
    }
    return res;
}

QVector<int> MainWindow::parseColInput(const QString& text)
{
    QVector<int> result;
    if (text.isEmpty()) return result;

    QString cleanText = text.trimmed().replace(" ", ",");
    QStringList parts = cleanText.split(",", TXT_VIEWER_SKIP_EMPTY_PARTS);

    for (const QString& part : parts) {
        bool ok;
        int col = part.toInt(&ok);
        if (ok && col > 0 && m_selectedReadCols.contains(col) && !result.contains(col)) {
            result.append(col);
            if (m_colBindMap.contains(col)) {
                int boundCol = m_colBindMap[col];
                if (!result.contains(boundCol)) result.append(boundCol);
            }
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

QString MainWindow::getSelectedColsText()
{
    QVector<int> checkedColumnIndices = m_checkableHeader->getCheckedColumns();
    QStringList strList;
    for (int idx : checkedColumnIndices) {
        if (idx >= 0 && idx < m_selectedReadCols.size()) {
            strList.append(QString::number(m_selectedReadCols[idx]));
        }
    }
    return strList.join(",");
}

void MainWindow::onHeaderCheckStateChanged(int column, bool checked)
{
    m_checkableHeader->blockSignals(true);
    int rawColNum = m_selectedReadCols.value(column, -1);
    if (rawColNum == -1) {
        m_checkableHeader->blockSignals(false);
        return;
    }

    m_colInputEdit->blockSignals(true);
    m_colInputEdit->setText(getSelectedColsText());
    m_colInputEdit->blockSignals(false);

    if (m_colBindMap.contains(rawColNum)) {
        int boundRawCol = m_colBindMap[rawColNum];
        int boundColumnIndex = getColumnIndexByRawNum(boundRawCol);
        if (boundColumnIndex != -1) {
            m_checkableHeader->setCheckState(boundColumnIndex, checked);
        }
    }

    m_checkableHeader->blockSignals(false);
}

void MainWindow::onColInputEditingFinished()
{
    QVector<int> inputCols = parseColInput(m_colInputEdit->text());
    m_checkableHeader->blockSignals(true);

    if (inputCols.isEmpty()) {
        m_checkableHeader->clearAllCheckStates();
    } else {
        for (int i = 0; i < m_selectedReadCols.size(); ++i) {
            m_checkableHeader->setCheckState(i, inputCols.contains(m_selectedReadCols[i]));
        }
    }

    m_checkableHeader->blockSignals(false);
}

void MainWindow::loadTxt()
{
    QString path = QFileDialog::getOpenFileName(
        this, "选择数据文件", QDir::homePath(),
        "TXT文件 (*.txt);;所有文件 (*.*)"
    );
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件");
        return;
    }

    QProgressDialog progress("正在加载数据...", "取消", 0, 100, this);
    progress.setWindowTitle("数据加载中");
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(500);
    progress.setStyleSheet(R"(
        QProgressDialog { font: 12pt Monospace; }
        QProgressBar { border: 1px solid #ccc; border-radius: 6px; height: 20px; }
        QProgressBar::chunk { background-color: #337ab7; border-radius: 4px; }
    )");

    m_loadBtn->setEnabled(false);
    m_plotBtn->setEnabled(false);
    m_multiPlotBtn->setEnabled(false);
    m_tableView->setUpdatesEnabled(false);

    QElapsedTimer timer;
    timer.start();

    QTextStream ts(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    ts.setEncoding(QStringConverter::Utf8);
#else
    ts.setCodec("UTF-8");
#endif

    QVector<QVector<QString>> data;
    QVector<QVector<double>> numericData;
    QVector<QVector<uchar>> numericValidData;
    const int BATCH_ROWS = 2000;
    data.reserve(BATCH_ROWS);
    numericData.reserve(BATCH_ROWS);
    numericValidData.reserve(BATCH_ROWS);
    qint64 fileTotalSize = f.size();
    qint64 fileReadSize = 0;

    while (!ts.atEnd()) {
        if (progress.wasCanceled()) {
            data.clear();
            f.close();
            m_tableView->setUpdatesEnabled(true);
            m_loadBtn->setEnabled(true);
            QMessageBox::information(this, "提示", "已取消数据加载");
            return;
        }

        QString line = ts.readLine();
        fileReadSize += line.size() + 2;
        line = line.trimmed();
        if (line.isEmpty()) continue;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        static const QRegularExpression whitespaceRe("\\s+");
        QStringList parts = line.split(whitespaceRe, TXT_VIEWER_SKIP_EMPTY_PARTS);
#else
        static const QRegExp whitespaceRe("\\s+");
        QStringList parts = line.split(whitespaceRe, TXT_VIEWER_SKIP_EMPTY_PARTS);
#endif
        QVector<QString> rowData;
        QVector<double> rowNumericData;
        QVector<uchar> rowNumericValidData;
        rowData.reserve(m_selectedReadCols.size());
        rowNumericData.reserve(m_selectedReadCols.size());
        rowNumericValidData.reserve(m_selectedReadCols.size());
        for (int col : m_selectedReadCols) {
            int idx = col - 1;
            QString value = (idx >= 0 && idx < parts.size()) ? parts[idx] : "";
            bool ok = false;
            double numericValue = value.toDouble(&ok);
            rowData.append(value);
            rowNumericData.append(ok ? numericValue : 0.0);
            rowNumericValidData.append(ok ? 1 : 0);
        }
        data.append(rowData);
        numericData.append(rowNumericData);
        numericValidData.append(rowNumericValidData);

        if (data.size() % BATCH_ROWS == 0) {
            data.reserve(data.size() + BATCH_ROWS);
            numericData.reserve(numericData.size() + BATCH_ROWS);
            numericValidData.reserve(numericValidData.size() + BATCH_ROWS);
            int readProgress = qMin((int)((fileReadSize * 50) / fileTotalSize), 50);
            progress.setValue(readProgress);
            progress.setLabelText(QString("正在读取文件...（仅处理指定列，已读%1%）").arg(readProgress * 2));
        }
    }
    f.close();

    if (data.isEmpty()) {
        m_tableView->setUpdatesEnabled(true);
        m_loadBtn->setEnabled(true);
        progress.close();
        QMessageBox::information(this, "提示", "文件无有效数据");
        return;
    }

    int rows = data.size();
    int cols = m_selectedReadCols.size();
    progress.setValue(55);
    progress.setLabelText("正在处理数据...（仅检测指定列不变值）");

    QVector<int> redRows(cols, -1);
    for (int c = 0; c < cols; ++c) {
        if (progress.wasCanceled()) {
            data.clear();
            m_tableView->setUpdatesEnabled(true);
            m_loadBtn->setEnabled(true);
            QMessageBox::information(this, "提示", "已取消数据加载");
            return;
        }

        int lastValidRow = -1;
        for (int r = rows - 1; r >= 0; --r) {
            if (c < numericValidData[r].size() && numericValidData[r][c]) {
                lastValidRow = r;
                break;
            }
        }
        if (lastValidRow <= 0) continue;

        double last = numericData[lastValidRow][c];
        int start = lastValidRow;
        for (int r = lastValidRow - 1; r >= 0; --r) {
            if (c < numericValidData[r].size() && numericValidData[r][c] && qFuzzyCompare(numericData[r][c], last)) start = r;
            else break;
        }
        if (start < lastValidRow) redRows[c] = start;

        int redProgress = 60 + qMin((int)((c * 30) / cols), 30);
        progress.setValue(redProgress);
    }

    progress.setValue(90);
    progress.setLabelText("正在初始化表格...（设置列宽+绑定模型）");
    m_rawData = std::move(data);
    m_numericData = std::move(numericData);
    m_numericValidData = std::move(numericValidData);
    QVector<int> widths = calculateColMaxWidth();
    m_tableModel->setData(&m_rawData, redRows, m_selectedReadCols);
    for (int c = 0; c < cols && c < widths.size(); ++c) {
        m_tableView->setColumnWidth(c, widths[c]);
    }
    m_checkableHeader->initCheckStates(cols);

    m_tableView->setUpdatesEnabled(true);
    m_startRowSpin->setRange(0, rows - 1);
    m_startRowSpin->setValue(0);
    m_startRowSpin->setEnabled(true);
    
    m_endRowSpin->setRange(0, rows - 1);
    m_endRowSpin->setValue(rows - 1);
    m_endRowSpin->setEnabled(true);

    m_plotBtn->setEnabled(true);
    m_multiPlotBtn->setEnabled(true);
    m_loadBtn->setEnabled(true);

    progress.setValue(100);
    progress.setLabelText("加载完成");
}

void MainWindow::plotSelectedCols()
{
    if (m_rawData.isEmpty()) return;

    onColInputEditingFinished();

    int start = m_startRowSpin->value();
    int end = m_endRowSpin->value();
    int rows = m_rawData.size();
    if (start < 0) start = 0;
    if (end >= rows) end = rows - 1;
    if (start > end) std::swap(start, end);

    int cols = m_selectedReadCols.size();

    QList<PlotData> list;
    QVector<int> checkedColumnIndices = m_checkableHeader->getCheckedColumns();
    if (checkedColumnIndices.isEmpty()) {
        QMessageBox::information(this, "提示", "请至少勾选一列（表头左侧）或输入有效列号");
        return;
    }

    for (int idx : checkedColumnIndices) {
        if (idx < 0 || idx >= cols) continue;
        int colNum = m_selectedReadCols[idx];
        PlotData pd;
        pd.colIndex = idx;
        pd.rawColNum = colNum;
        pd.colLabel = m_tableModel->getColumnLabel(idx);
        pd.groupIndex = m_colToGroupIndex.value(colNum, 0);
        pd.yInverted = false;
        pd.yScale = 1.0;
        pd.visible = true;
        
        pd.yData.reserve(end - start + 1);
        for (int r = start; r <= end; ++r) {
            double y = 0.0;
            if (r < m_numericData.size() && idx < m_numericData[r].size() &&
                r < m_numericValidData.size() && idx < m_numericValidData[r].size() &&
                m_numericValidData[r][idx]) {
                y = m_numericData[r][idx];
            }
            pd.yData << y;
        }
        list << pd;
    }

    if (list.isEmpty()) {
        QMessageBox::information(this, "提示", "所选列无有效数据");
        return;
    }

    PlotDialog* dlg = new PlotDialog(this, list, &m_numericData, &m_numericValidData, &m_selectedReadCols, &m_colIndexByRawNum, &m_colToGroupIndex, start, end);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QObject::destroyed, this, [this, dlg]() {
        if (m_currentPlotDialog == dlg) m_currentPlotDialog = nullptr;
    });
    dlg->show();
    m_currentPlotDialog = dlg;

    m_checkableHeader->clearAllCheckStates();
    m_colInputEdit->clear();
}

void MainWindow::plotMultiCanvasByCode()
{
    if (m_rawData.isEmpty()) return;

    int start = m_startRowSpin->value();
    int end = m_endRowSpin->value();
    int rows = m_rawData.size();
    if (start < 0) start = 0;
    if (end >= rows) end = rows - 1;
    if (start > end) std::swap(start, end);

    int cols = m_selectedReadCols.size();

    QMap<int, QList<int>> groupCols;
    for (int col : m_selectedReadCols) {
        int groupIdx = m_colToGroupIndex.value(col, 0);
        groupCols[groupIdx].append(col);
    }

    static int off = 0;
    for (auto it = groupCols.begin(); it != groupCols.end(); ++it) {
        int groupIdx = it.key();
        const QList<int>& groupColList = it.value();
        
        QList<PlotData> list;
        for (int colNum : groupColList) {
            int idx = getColumnIndexByRawNum(colNum);
            if (idx == -1 || idx >= cols) continue;

            PlotData pd;
            pd.colIndex = idx;
            pd.rawColNum = colNum;
            pd.colLabel = m_tableModel->getColumnLabel(idx);
            pd.groupIndex = groupIdx;
            pd.yInverted = false;
            pd.yScale = 1.0;
            pd.visible = true;
            
            pd.yData.reserve(end - start + 1);
            for (int r = start; r <= end; ++r) {
                double y = 0.0;
                if (r < m_numericData.size() && idx < m_numericData[r].size() &&
                    r < m_numericValidData.size() && idx < m_numericValidData[r].size() &&
                    m_numericValidData[r][idx]) {
                    y = m_numericData[r][idx];
                }
                pd.yData << y;
            }
            list << pd;
        }

        if (list.isEmpty()) continue;
        
        PlotDialog* dlg = new PlotDialog(this, list, &m_numericData, &m_numericValidData, &m_selectedReadCols, &m_colIndexByRawNum, &m_colToGroupIndex, start, end);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(QString("分组画布%1（组%2）").arg(++off).arg(groupIdx));
        dlg->show();
    }

    m_checkableHeader->clearAllCheckStates();
    m_colInputEdit->clear();
}
