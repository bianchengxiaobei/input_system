#pragma once

#include "insys/low/fourcc.h"

namespace insys {

inline constexpr FourCC kFormatBit = FourCC('B', 'I', 'T', ' ');
inline constexpr FourCC kFormatByte = FourCC('B', 'Y', 'T', 'E');
inline constexpr FourCC kFormatInt = FourCC('I', 'T', 'R', ' ');
inline constexpr FourCC kFormatUShort = FourCC('U', 'S', 'H', 'T');
inline constexpr FourCC kFormatUInt = FourCC('U', 'I', 'N', 'T');
inline constexpr FourCC kFormatFloat = FourCC('F', 'L', 'T', ' ');
inline constexpr FourCC kFormatDouble = FourCC('D', 'B', 'L', ' ');
inline constexpr FourCC kFormatVector2 = FourCC('V', 'E', 'C', '2');
inline constexpr FourCC kFormatVector3 = FourCC('V', 'E', 'C', '3');
inline constexpr FourCC kFormatQuaternion = FourCC('Q', 'U', 'A', 'T');
inline constexpr FourCC kFormatKeyboard = FourCC('K', 'E', 'Y', ' ');
inline constexpr FourCC kFormatMouse = FourCC('M', 'O', 'U', ' ');
inline constexpr FourCC kFormatGamepad = FourCC('G', 'P', 'A', 'D');
inline constexpr FourCC kFormatJoystick = FourCC('J', 'O', 'Y', 'S');
inline constexpr FourCC kFormatGyroscope = FourCC('G', 'Y', 'R', 'O');
inline constexpr FourCC kFormatAccelerometer = FourCC('A', 'C', 'C', 'L');
inline constexpr FourCC kFormatGravity = FourCC('G', 'R', 'A', 'V');
inline constexpr FourCC kFormatLinearAcceleration = FourCC('L', 'A', 'A', 'C');
inline constexpr FourCC kFormatMagneticField = FourCC('M', 'G', 'N', 'F');
inline constexpr FourCC kFormatAttitude = FourCC('A', 'T', 'T', 'I');
inline constexpr FourCC kFormatTouchscreen = FourCC('T', 'O', 'U', 'C');
inline constexpr FourCC kFormatLight = FourCC('L', 'I', 'G', 'T');
inline constexpr FourCC kFormatPressure = FourCC('P', 'R', 'E', 'S');
inline constexpr FourCC kFormatProximity = FourCC('P', 'R', 'O', 'X');
inline constexpr FourCC kFormatHumidity = FourCC('H', 'U', 'M', 'I');
inline constexpr FourCC kFormatAmbientTemperature = FourCC('T', 'E', 'M', 'P');
inline constexpr FourCC kFormatStepCounter = FourCC('S', 'T', 'E', 'P');
inline constexpr FourCC kFormatHingeAngle = FourCC('H', 'I', 'N', 'G');
inline constexpr FourCC kFormatPen = FourCC('P', 'E', 'N', ' ');
inline constexpr FourCC kFormatTrackedDevice = FourCC('T', 'R', 'C', 'K');
inline constexpr FourCC kFormatHid = FourCC('H', 'I', 'D', ' ');
inline constexpr FourCC kFormatHidGamepad = FourCC('H', 'G', 'P', 'D');

} // namespace insys




