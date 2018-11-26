/*
This file is part of Ionlib.  Copyright (C) 2018  Tim Sweet

Ionlib is free software : you can redistribute it and / or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Ionlib is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Ionlib.If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef EASYAPCONFIG_H
#define EASYAPCONFIG_H
#include <stdint.h>

#define MAX_AP_NAME_LEN (32)
#define MAX_AP_PASSWORD_LEN (63)

class EasyAPConfig
{
public:
    EasyAPConfig(uint16_t configStartAddr);
    void Connect(const char* setupAPName, uint32_t timeout = UINT32_MAX);
private:
    char apName_[MAX_AP_NAME_LEN];
    char apPassword_[MAX_AP_PASSWORD_LEN];
    uint16_t cfgStartAddr_;
};
#endif
