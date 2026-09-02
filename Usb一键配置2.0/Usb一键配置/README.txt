查看当前串口候选：
./diagnose
只查看设备列表：
bash phybot-usb-setup.sh list
不知道哪个口是IMU时，主动探测：
sudo bash phybot-usb-setup.sh probe
探测会对IMU候选口发送 `LOG VERSION`，并尝试 `921600`、`115200`、`460800`、`230400`、`9600`。
输出里出现 `FOUND` 的口就是IMU，安装配置时选择这个设备路径。
不知道哪个口是航模手柄时，主动探测：
sudo bash phybot-usb-setup.sh joy-probe
航模探测只检查 `/dev/ttyUSB0` 到 `/dev/ttyUSB5`，会先排除已识别为 IMU 的口，再按 SpeedRun 项目使用的 `100000 8E2` 读取 25 字节 SBUS 帧。
输出里出现 `FOUND` 的口就是航模手柄，安装配置时选择这个设备路径。
安装配置：
sudo bash phybot-usb-setup.sh
安装时会先按HiPNUC协议主动探测IMU，也会按SBUS数据主动探测航模手柄。
找到唯一设备时，直接回车即可选择推荐端口。
脚本会先列出IMU候选口，IMU支持：
`/dev/ttyUSB0` 到 `/dev/ttyUSB5`
`/dev/ttyACM0` 到 `/dev/ttyACM5`
`/dev/ttyS0` 到 `/dev/ttyS9`
`/dev/ttyAMA0` 到 `/dev/ttyAMA9`
`/dev/ttyTHS0` 到 `/dev/ttyTHS9`
航模手柄仍然只从 `/dev/ttyUSB0` 到 `/dev/ttyUSB5` 中选择，避免误把板载UART配成航模口。
配置完成后，程序里应使用稳定链接：
IMU：`/dev/ttyimu`
航模：`/dev/ttyjoy`
USB设备会优先按 `ID_PATH` 或 `ID_SERIAL_SHORT` 生成稳定规则，避免重启后 `/dev/ttyUSB0`、`/dev/ttyUSB1`、`/dev/ttyUSB4` 这类临时编号变化导致失效。
UART设备通常没有USB物理路径，脚本会按UART内核设备名生成 `/dev/ttyimu`。
查看配置结果：
bash phybot-usb-setup.sh status
删除配置：
bash phybot-usb-remove.sh
删除脚本会自动请求sudo权限，同时清理当前规则和旧版IMU/joy规则。
如果更换了USB设备的物理插口，或IMU改接到另一个UART口，需要重新运行安装配置。
配置写入：`/etc/udev/rules.d/99-phybot-imu.rules`
