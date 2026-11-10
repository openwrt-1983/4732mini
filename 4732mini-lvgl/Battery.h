

#ifndef Battery_H
#define Battery_H

#include "Arduino.h"

#define ADCPIN 4
#define READS 20


  double getBatteryVolts(double conversionFactor, bool enable);  //加入double conversionFactor 每次读取电压都重新新设置 系数 用于校准电压
  int getBatteryChargeLevel(double conversionFactor , bool useConversionTable=false) ;


#endif
