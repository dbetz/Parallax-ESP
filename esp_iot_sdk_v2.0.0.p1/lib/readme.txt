ESP8266_NONOS_SDK_V2.0.0_patch 发布说明
本patch基于ESP8266_NONOS_SDK_V2.0.0，使用时将压缩包中.a文件覆盖SDK/lib目录下对应文件。
有如下更新：
1. 修正在某些情况下连接较慢问题。
2. 提供使能频偏自动校准接口：void system_phy_freq_trace_enable(bool enable); 频偏自动校准功能默认为关闭，如客户需要打开该功能，需要在user_rf_pre_init()里调用该函数，参数为true。


注：客户需要确认：使用场景中的环境温度超出了所用晶振的正常工作温度范围，才需要开启此功能。一般情况下，使用场景环境温度小于80度不建议开启。

_____________________________________________________________________________________

ESP8266_NONOS_SDK_V2.0.0_patch release notes

This patch is based on ESP8266_NONOS_SDK_V2.0.0. While using it, users should use .a file in the package to overwrite the corresponding files subject to SDK/lib directory.
The updates are as follows:
1. Slow connection problems in some cases have been solved.
2. Provided an interface to enable automatic calibration of frequency offset: void system_phy_freq_trace_enable(bool enable); By default, the automatic calibration of frequency offset is off. To enable it, this function should be called in user_rf_pre_init(). Its parameter is true.


Note: Users should confirm that only when ambient temperature is higher than the normal operating temperature range that crystal oscillator requires does this function need to be enabled. In general,  this function is not recommended to be enabled if ambient temperature is lower than 80 ℃.