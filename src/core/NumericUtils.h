#pragma once
#include <Arduino.h>
#include <stddef.h>

namespace hp::numeric {

// Stateless, shared number operations for app presentation and controls.
bool calculate(char operation, double left, double right, double& result);
double roundTo(double value, uint8_t decimals);
String fixed(double value, uint8_t decimals, const char* fallback = "-");
bool general(double value, char* target, size_t targetSize);

int percentOf(int value, int maximum);
int percentToValueRounded(int percent, int maximum);
int percentToValueFloor(int percent, int maximum);

}
