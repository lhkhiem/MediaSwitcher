#pragma once

#include <QString>

struct CopyrightInfo {
    QString footerText;
    QString details;
};

class CopyrightSettings {
public:
    static CopyrightInfo load();
};
