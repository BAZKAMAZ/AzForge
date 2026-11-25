#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <string>

// In-memory Qt pixmap/icon cache for subsystem icons loaded from config/ or ~/.bazkamaz/packages/*.zip.
class IconCache {
public:
    static IconCache& Instance();

    // Returns a cached scaled pixmap for a subsystem id (or a crisp monogram badge if no PNG exists).
    QPixmap SubsystemPixmap(const std::string& subsystemId, int size = 16);
    QIcon SubsystemIcon(const std::string& subsystemId, int size = 16);

    // Clears cached entries (call after reloading packages/catalog).
    void Clear();

private:
    IconCache() = default;
    QPixmap LoadRaw(const std::string& iconPath);
    QPixmap MakeBadge(const std::string& subsystemId, int size);
};
