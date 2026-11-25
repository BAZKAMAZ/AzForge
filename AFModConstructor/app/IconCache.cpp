#include "IconCache.h"

#include "ModCatalog.h"
#include "ModPackages.h"
#include "PathUtil.h"
#include "UiFormat.h"

#include <QPainter>
#include <QPixmapCache>

#include <unordered_map>
#include <vector>

namespace {

std::unordered_map<std::string, QPixmap> g_rawCache;
std::unordered_map<std::string, QPixmap> g_scaledCache;

} // namespace

IconCache& IconCache::Instance() {
    static IconCache inst;
    return inst;
}

void IconCache::Clear() {
    g_rawCache.clear();
    g_scaledCache.clear();
    QPixmapCache::clear();
}

QPixmap IconCache::LoadRaw(const std::string& iconPath) {
    if (iconPath.empty()) return {};

    const auto it = g_rawCache.find(iconPath);
    if (it != g_rawCache.end()) return it->second;

    QPixmap pm;
    if (iconPath.rfind("pkg:", 0) == 0) {
        const size_t colon = iconPath.find(':', 4);
        if (colon != std::string::npos) {
            const std::string pkgId = iconPath.substr(4, colon - 4);
            const std::string inner = iconPath.substr(colon + 1);
            if (const ModPackages::Package* pkg = ModPackages::Find(pkgId)) {
                std::vector<unsigned char> bytes;
                if (ModPackages::ReadFile(*pkg, inner, bytes, nullptr) && !bytes.empty()) {
                    pm.loadFromData(bytes.data(), static_cast<uint>(bytes.size()));
                }
            }
        }
    } else {
        const auto full = PathUtil::MakePath(ModCatalog::Dir()) / PathUtil::MakePath(iconPath);
        pm.load(QString::fromStdWString(full.wstring()));
    }

    // Cache failures too so disk/zip is never hit repeatedly on paint
    g_rawCache.emplace(iconPath, pm);
    return pm;
}

QPixmap IconCache::MakeBadge(const std::string& subsystemId, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor col = UiFormat::SubsystemColor(subsystemId);
    QColor fill = col;
    fill.setAlpha(42);
    p.setBrush(fill);
    p.setPen(QPen(col, 1.0));
    p.drawRoundedRect(QRectF(0.5, 0.5, size - 1.0, size - 1.0), 3.0, 3.0);

    QString tag = QString::fromStdString(subsystemId).left(2).toUpper();
    if (tag.isEmpty()) tag = QStringLiteral("M");
    QFont f;
    f.setPixelSize(std::max(8, size / 2));
    f.setBold(true);
    p.setFont(f);
    p.setPen(col.lighter(120));
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, tag);
    return pm;
}

QPixmap IconCache::SubsystemPixmap(const std::string& subsystemId, int size) {
    const std::string key = subsystemId + "#" + std::to_string(size);
    const auto it = g_scaledCache.find(key);
    if (it != g_scaledCache.end()) return it->second;

    QPixmap result;
    if (const ModCatalog::SubsystemDef* def = ModCatalog::FindSubsystem(subsystemId)) {
        const QPixmap raw = LoadRaw(def->icon);
        if (!raw.isNull()) {
            result = raw.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    if (result.isNull()) {
        result = MakeBadge(subsystemId, size);
    }

    g_scaledCache.emplace(key, result);
    return result;
}

QIcon IconCache::SubsystemIcon(const std::string& subsystemId, int size) {
    return QIcon(SubsystemPixmap(subsystemId, size));
}
