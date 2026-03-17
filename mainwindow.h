#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QMainWindow>
#include <QTableView>
#include <QAbstractTableModel>
#include <QPushButton>
#include <QSpinBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QPolygonF>
#include <QFont>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QList>
#include <QVector>
#include <QString>
#include <QLabel>
#include <QResizeEvent>
#include <QLineEdit>
#include <QHeaderView>
#include <QCheckBox>
#include <QModelIndex>
#include <QShortcut>
#include <QMap>
#include <QComboBox>
#include <QDoubleValidator>

struct PlotData {
    int colIndex;
    int rawColNum;
    QVector<double> xData;
    QVector<double> yData;
    QString colLabel;
    int groupIndex;
    bool yInverted;
    double yScale;
    bool visible;
};

class CheckableHeaderView : public QHeaderView
{
    Q_OBJECT
public:
    explicit CheckableHeaderView(QMap<int, int>* colGroupMap, QVector<int>* displayColNums,
                                 Qt::Orientation orientation, QWidget *parent = nullptr);

    void setCheckState(int column, bool checked);
    bool getCheckState(int column) const;
    QVector<int> getCheckedColumns() const;
    void initCheckStates(int columnCount);
    void clearAllCheckStates();

signals:
    void checkStateChanged(int column, bool checked);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QColor getGroupHeaderBgColor(int logicalIndex) const;

    QVector<bool> m_checkedStates;
    QMap<int, int>* m_colGroupMap;
    QVector<int>* m_displayColNums;
};

class BigDataTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit BigDataTableModel(QObject *parent = nullptr);

    void setData(const QVector<QVector<QString>>& data, const QVector<int>& redColStartRows, const QVector<int>& displayColNums);
    void setColGroupMap(const QMap<int, int>& map);
    QString getData(int row, int col) const;
    QString getColumnLabel(int column) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QVector<QVector<QString>> m_rawData;
    QVector<int> m_redColStartRows;
    QVector<int> m_displayColNums;
    QMap<int, int> m_colGroupMap;
};

class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlotWidget(QWidget *parent = nullptr);

    void setData(const QList<PlotData>& data);
    void resetView();
    void setCurveYInverted(int curveIdx, bool inverted);
    void setCurveYScale(int curveIdx, double scale);
    void setCurveVisible(int curveIdx, bool visible);
    void setAllCurvesVisible(bool visible);
    QStringList getCurveLabels() const;

    QList<PlotData> m_plotData;

signals:
    void curveInvertClicked(int curveIdx);
    void curveVisibilityChanged(int curveIdx, bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateBackgroundCache();
    void updateCurveCache();
    void updateLegendCache();
    double interpolateY(const QVector<double>& xData, const QVector<double>& yData, double targetX);
    void calculateOriginalRange();
    void getPlotRect(int& marginL, int& marginT, int& marginR, int& marginB, int& plotW, int& plotH);
    
    // 修改：替换为特征点保留算法
    QVector<QPointF> sampleData(const QVector<double>& xData, const QVector<double>& yData, double pixelEpsilon);
    void douglasPeucker(const QVector<QPointF>& points, int start, int end, double epsilon, QVector<QPointF>& result);
    
    double processYValue(int curveIdx, double y);
    void updatePlotRange();

    QPoint m_mousePos;
    bool m_mouseInWidget;
    QPoint m_dragStart;
    QPoint m_dragEnd;
    bool m_isDragging;
    double m_originalXMin, m_originalXMax;
    double m_currentXMin, m_currentXMax;
    double m_originalYMin, m_originalYMax;
    double m_currentYMin, m_currentYMax;
    
    QPixmap m_backgroundCache;
    QPixmap m_curveCache;
    QPixmap m_legendCache;
    
    int m_hoveredCurveIdx;
    QList<QRectF> m_tipTextRects;
    QList<QRectF> m_checkBoxRects;

    const QList<QColor> m_curveColors = {
        QColor(255, 0, 0), QColor(0, 0, 0), QColor(0, 0, 255),
        QColor(0, 255, 0), QColor(255, 165, 0), QColor(128, 0, 128)
    };
};

class PlotDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PlotDialog(QWidget *parent = nullptr, const QList<PlotData>& plotData = QList<PlotData>());
    ~PlotDialog();

private slots:
    void onZoomInClicked();
    void onResetClicked();
    void onApplyScaleClicked();
    void onCurveInvertClicked(int curveIdx);
    void onHideAllClicked();
    void onShowAllClicked();

private:
    PlotWidget* m_plotWidget;
    QPushButton* m_zoomBtn;
    QPushButton* m_resetBtn;
    QPushButton* m_hideAllBtn;
    QPushButton* m_showAllBtn;
    bool m_zoomMode;
    QComboBox* m_curveCombo;
    QLineEdit* m_scaleEdit;
    QPushButton* m_applyScaleBtn;
    QLabel* m_scaleLabel;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void loadTxt();
    void plotSelectedCols();
    void plotMultiCanvasByCode();
    void onColInputEditingFinished();
    void onHeaderCheckStateChanged(int column, bool checked);
    void closeCurrentPlotDialog();

private:
    void initUI();
    bool isNumber(const QString& str) const;
    QVector<int> calculateColMaxWidth() const;
    QVector<int> parseColInput(const QString& text);
    QString getSelectedColsText();
    int getColumnIndexByRawNum(int rawColNum) const;

    QTableView* m_tableView;
    BigDataTableModel* m_tableModel;
    CheckableHeaderView* m_checkableHeader;
    QPushButton* m_loadBtn;
    QPushButton* m_plotBtn;
    QPushButton* m_multiPlotBtn;
    QSpinBox* m_startRowSpin;
    QSpinBox* m_endRowSpin; 
    QLineEdit* m_colInputEdit;
    QLabel* m_colInputLbl;
    QVector<QVector<QString>> m_rawData;
    QVector<int> m_selectedReadCols;
    QMap<int, int> m_colToGroupIndex;
    QMap<int, int> m_colBindMap;
    PlotDialog* m_currentPlotDialog;
};
#endif // MAINWINDOW_H
