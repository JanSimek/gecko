#pragma once

#include <memory>

#include <QDialog>
#include <QPixmap>
#include <QString>

class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QObject;
class QPushButton;
class QShowEvent;
class QTimer;

namespace geck {

class HexagonGrid;
namespace resource {
    class GameResources;
}

/// Browses the maps available in the mounted game data as a thumbnail grid, with a
/// filter box and a larger preview of the highlighted map. Thumbnails are rendered
/// lazily — only for the cells currently in view, one per event-loop turn — because
/// each one parses and renders a whole map, which is far too heavy to do up front for
/// the hundreds of maps a DAT can hold. Picking a map (double-click or Open) accepts
/// the dialog; its VFS path is then available via selectedMapPath().
class MapBrowserDialog : public QDialog {
    Q_OBJECT

public:
    explicit MapBrowserDialog(resource::GameResources& resources, QWidget* parent = nullptr);
    ~MapBrowserDialog() override;

    /// VFS path of the chosen map (e.g. "maps/sfshutl2.map"); empty unless accepted.
    QString selectedMapPath() const { return _selectedPath; }

protected:
    void showEvent(QShowEvent* event) override;
    // Rescales the preview to fit when the preview label is resized (e.g. by the splitter).
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onFilterChanged(const QString& text);
    void onCurrentItemChanged(QListWidgetItem* current);
    void onItemActivated(const QListWidgetItem* item);
    void renderNextVisibleThumbnail();

private:
    void populate();
    void acceptCurrent();
    void updatePreview(const QListWidgetItem* item);
    void rescalePreview();
    QListWidgetItem* nextUnrenderedVisibleItem() const;

    resource::GameResources& _resources;
    std::unique_ptr<HexagonGrid> _hexgrid;
    QLineEdit* _search = nullptr;
    QListWidget* _grid = nullptr;
    QLabel* _previewImage = nullptr;
    QLabel* _previewName = nullptr;
    QPushButton* _openButton = nullptr;
    QTimer* _thumbnailTimer = nullptr;
    QPixmap _previewSource; ///< Native-resolution preview, rescaled to fit on resize.
    QString _selectedPath;
};

} // namespace geck
