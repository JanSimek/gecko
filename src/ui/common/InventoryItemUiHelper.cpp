#include "InventoryItemUiHelper.h"

#include "../../format/frm/Frm.h"
#include "../../format/map/MapObject.h"
#include "../../format/msg/Msg.h"
#include "../../format/pro/Pro.h"
#include "../../util/FalloutEngineEnums.h"
#include "../../util/ProHelper.h"
#include "../../util/ResourceManager.h"

#include <QImage>
#include <QPainter>
#include <QSize>
#include <QPixmap>

#include <spdlog/spdlog.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace geck::ui::inventory {
namespace {

QString formatPid(uint32_t pid) {
    return QString("0x%1").arg(pid, 8, 16, QChar('0'));
}

Pro* loadPro(uint32_t pid) {
    return ResourceManager::getInstance().loadResource<Pro>(ProHelper::basePath(pid));
}

bool isAmmoItem(const Pro& pro) {
    return pro.type() == Pro::OBJECT_TYPE::ITEM && pro.itemType() == Pro::ITEM_TYPE::AMMO;
}

QString typeNameForPro(const Pro& pro) {
    if (pro.type() == Pro::OBJECT_TYPE::ITEM) {
        Msg* protoMsg = ProHelper::protoMsgFile();
        if (!protoMsg) {
            throw std::runtime_error("proto.msg is not loaded");
        }

        auto itemType = static_cast<fallout::ItemType>(static_cast<int>(pro.itemType()));
        const std::string text = protoMsg->message(fallout::protoMessageId(itemType)).text;
        return QString::fromStdString(text);
    }

    return QString::fromStdString(pro.typeToString());
}

QString nameForPid(const Pro& pro) {
    Msg* msg = ProHelper::msgFile(pro.type());
    if (!msg) {
        throw std::runtime_error("PRO message file is not loaded");
    }

    const std::string text = msg->message(pro.header.message_id).text;
    if (text.empty()) {
        throw std::runtime_error("PRO message is empty");
    }

    return QString::fromStdString(text);
}

std::string resolveFrmPath(uint32_t pid, const Pro& pro) {
    auto& resourceManager = ResourceManager::getInstance();

    if (pro.type() == Pro::OBJECT_TYPE::ITEM && pro.commonItemData.inventoryFID > 0) {
        try {
            return resourceManager.FIDtoFrmName(pro.commonItemData.inventoryFID);
        } catch (const std::exception& e) {
            spdlog::debug("InventoryItemUiHelper: Inventory FID resolution failed for PID {}: {}", pid, e.what());
        }
    }

    if (pro.header.FID > 0) {
        return resourceManager.FIDtoFrmName(pro.header.FID);
    }

    return {};
}

QPixmap renderFramePixmap(const std::string& frmPath, int iconSize, bool fixedCanvas) {
    auto& resourceManager = ResourceManager::getInstance();
    Frm* frm = resourceManager.loadResource<Frm>(frmPath);
    if (!frm) {
        return QPixmap();
    }

    const auto& directions = frm->directions();
    if (directions.empty() || directions[0].frames().empty()) {
        return QPixmap();
    }

    const auto& frame = directions[0].frames()[0];
    const sf::Texture& texture = resourceManager.texture(frmPath);
    sf::Image image = texture.copyToImage();

    QImage fullImage(
        reinterpret_cast<const uchar*>(image.getPixelsPtr()),
        static_cast<int>(image.getSize().x),
        static_cast<int>(image.getSize().y),
        QImage::Format_RGBA8888);
    QImage frameImage = fullImage.copy(0, 0, frame.width(), frame.height()).copy();
    QPixmap pixmap = QPixmap::fromImage(frameImage);

    if (pixmap.isNull() || iconSize <= 0) {
        return pixmap;
    }

    QPixmap scaled = pixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!fixedCanvas) {
        return scaled;
    }

    QPixmap canvas(iconSize, iconSize);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const int x = (iconSize - scaled.width()) / 2;
    const int y = (iconSize - scaled.height()) / 2;
    painter.drawPixmap(x, y, scaled);

    return canvas;
}

} // namespace

uint32_t displayAmount(const MapObject& item) {
    try {
        Pro* pro = loadPro(item.pro_pid);
        if (!pro) {
            return item.amount;
        }

        if (isAmmoItem(*pro)) {
            uint64_t totalRounds = item.ammo;
            if (item.amount > 1) {
                totalRounds += static_cast<uint64_t>(pro->ammoData.quantity) * (item.amount - 1);
            }
            return totalRounds > std::numeric_limits<uint32_t>::max()
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(totalRounds);
        }

        return item.amount;
    } catch (const std::exception& e) {
        spdlog::warn("InventoryItemUiHelper::displayAmount: Failed to calculate display amount for PID {}: {}", item.pro_pid, e.what());
        return item.amount;
    }
}

ItemDetails describeItem(uint32_t pid) {
    ItemDetails details {
        .name = QString("Item %1").arg(pid, 8, 16, QChar('0')),
        .typeName = "Unknown",
        .pidText = formatPid(pid),
    };

    try {
        Pro* pro = loadPro(pid);
        if (!pro) {
            return details;
        }

        details.name = nameForPid(*pro);
        details.typeName = typeNameForPro(*pro);
    } catch (const std::exception& e) {
        spdlog::warn("InventoryItemUiHelper::describeItem: Failed to describe PID {}: {}", pid, e.what());
    }

    return details;
}

QPixmap loadItemIcon(uint32_t pid, int iconSize, bool fixedCanvas) {
    try {
        Pro* pro = loadPro(pid);
        if (!pro) {
            return QPixmap();
        }

        const std::string frmPath = resolveFrmPath(pid, *pro);
        if (frmPath.empty()) {
            return QPixmap();
        }

        return renderFramePixmap(frmPath, iconSize, fixedCanvas);
    } catch (const std::exception& e) {
        spdlog::debug("InventoryItemUiHelper::loadItemIcon: Failed to load icon for PID {}: {}", pid, e.what());
    }

    return QPixmap();
}

bool itemExists(uint32_t pid) {
    try {
        return loadPro(pid) != nullptr;
    } catch (const std::exception&) {
        return false;
    }
}

std::unique_ptr<MapObject> createMapInventoryItem(uint32_t pid, int amount) {
    if (amount < 1) {
        throw std::invalid_argument("Inventory item amount must be positive");
    }

    if (!loadPro(pid)) {
        throw std::runtime_error("Inventory item PRO does not exist");
    }

    auto item = std::make_unique<MapObject>();
    item->pro_pid = pid;
    item->frm_pid = 0;
    item->position = 0;
    item->elevation = 0;
    item->direction = 0;
    item->amount = static_cast<uint32_t>(amount);
    item->objects_in_inventory = 0;
    item->max_inventory_size = 0;
    item->inventory.clear();

    return item;
}

} // namespace geck::ui::inventory
