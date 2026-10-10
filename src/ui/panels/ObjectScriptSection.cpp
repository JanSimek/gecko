#include "ObjectScriptSection.h"

#include "format/map/Map.h"
#include "format/map/MapObject.h"
#include "format/map/MapScript.h"
#include "resource/ScriptSourceFacts.h"
#include "ui/dialogs/ScriptSelectorDialog.h"
#include "ui/theme/ThemeManager.h"
#include "ui/widgets/ElidedLabel.h"

#include <QCompleter>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <optional>

namespace geck {

namespace {

    constexpr int PROGRAM_INDEX_ROLE = Qt::UserRole + 1;
    constexpr int LOCAL_VARS_MAX_ROWS = 8;

    QString catalogText(const ScriptSelectorDialog::Entry& entry) {
        QString text = QString::fromStdString(entry.filename);
        const std::string& label = entry.name.empty() ? entry.comment : entry.name;
        if (!label.empty()) {
            text += " — " + QString::fromStdString(label);
        }
        if (!entry.name.empty() && !entry.comment.empty() && entry.comment != entry.name) {
            text += " (" + QString::fromStdString(entry.comment) + ")";
        }
        return text;
    }

    QString fieldName(resource::ScriptSourceFacts::Override::Field field) {
        return field == resource::ScriptSourceFacts::Override::Field::AiPacket ? "AI packet" : "team";
    }

} // namespace

ObjectScriptSection::ObjectScriptSection(resource::GameResources& resources, QWidget* parent)
    : QWidget(parent)
    , _resources(resources)
    , _picker(new QLineEdit(this))
    , _completer(new QCompleter(this))
    , _catalog(new QStandardItemModel(this))
    , _browseButton(new QToolButton(this))
    , _moreButton(new QToolButton(this))
    , _identity(new ElidedLabel(Qt::ElideRight, this))
    , _warnings(new QLabel(this))
    , _hint(new QLabel(this))
    , _localVarsTitle(new QLabel("Local variables", this))
    , _localVars(new QTreeWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ui::theme::spacing::TIGHT);

    // Picker row: type to search scripts.lst; choosing a match attaches it.
    _picker->setPlaceholderText("None — type to attach a script");
    _picker->setClearButtonEnabled(false);
    _picker->setMinimumWidth(0);
    _completer->setModel(_catalog);
    _completer->setCaseSensitivity(Qt::CaseInsensitive);
    _completer->setFilterMode(Qt::MatchContains);
    _completer->setCompletionRole(Qt::DisplayRole);
    _completer->setMaxVisibleItems(12);
    _picker->setCompleter(_completer);

    _browseButton->setText("…");
    _browseButton->setToolTip("Browse all scripts");
    _moreButton->setText("⋯");
    _moreButton->setToolTip("More script actions");
    _moreButton->setPopupMode(QToolButton::InstantPopup);
    _moreButton->setStyleSheet("QToolButton::menu-indicator { image: none; }"); // "⋯" already says menu
    auto* menu = new QMenu(_moreButton);
    _openSourceAction = menu->addAction("Open Source");
    _showInPanelAction = menu->addAction("Show in Scripts Panel");
    menu->addSeparator();
    _detachAction = menu->addAction("Detach");
    _moreButton->setMenu(menu);

    auto* pickerRow = new QHBoxLayout();
    pickerRow->setSpacing(ui::theme::spacing::TIGHT);
    pickerRow->addWidget(_picker, 1);
    pickerRow->addWidget(_browseButton);
    pickerRow->addWidget(_moreButton);
    layout->addLayout(pickerRow);

    _identity->setStyleSheet(ui::theme::styles::smallLabel());
    _identity->setToolTip("");
    layout->addWidget(_identity);

    for (QLabel* label : { _warnings, _hint }) {
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
        label->setMinimumWidth(0);
        label->setVisible(false);
        layout->addWidget(label);
    }
    _warnings->setStyleSheet(ui::theme::styles::statusWarning());
    _hint->setStyleSheet(ui::theme::styles::helpText());

    _localVarsTitle->setStyleSheet(ui::theme::styles::smallLabel());
    _localVars->setColumnCount(2);
    _localVars->setHeaderLabels({ "Variable", "Value" });
    _localVars->setRootIsDecorated(false);
    _localVars->setUniformRowHeights(true);
    _localVars->setSelectionMode(QAbstractItemView::NoSelection);
    _localVars->setMinimumWidth(0);
    _localVars->header()->setStretchLastSection(false);
    _localVars->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _localVars->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(_localVarsTitle);
    layout->addWidget(_localVars);
    _localVarsTitle->setVisible(false);
    _localVars->setVisible(false);

    connect(_completer, QOverload<const QModelIndex&>::of(&QCompleter::activated), this,
        [this](const QModelIndex& index) { choose(index.data(PROGRAM_INDEX_ROLE).toInt()); });
    // A search that ended without a choice puts the attached script back.
    connect(_picker, &QLineEdit::editingFinished, this, [this]() { showPickerText(); });
    connect(_picker, &QLineEdit::textEdited, this, [this]() { ensureCatalog(); });
    connect(_browseButton, &QToolButton::clicked, this, &ObjectScriptSection::browse);
    connect(_openSourceAction, &QAction::triggered, this, [this]() {
        if (_programIndex >= 0) {
            Q_EMIT editSourceRequested(_programIndex);
        }
    });
    connect(_showInPanelAction, &QAction::triggered, this, [this]() {
        if (_object && _object->map_scripts_pid != -1) {
            Q_EMIT showInScriptsPanelRequested(static_cast<uint32_t>(_object->map_scripts_pid));
        }
    });
    connect(_detachAction, &QAction::triggered, this, [this]() { Q_EMIT detachRequested(); });
}

void ObjectScriptSection::showPickerText() {
    const QSignalBlocker blocker(_picker);
    _picker->setText(_shownText);
    _picker->setCursorPosition(0); // the script's name leads; a long description runs off the end
}

void ObjectScriptSection::setStatusProvider(StatusProvider provider) {
    _statusProvider = std::move(provider);
}

void ObjectScriptSection::ensureCatalog() {
    if (_catalog->rowCount() > 0) {
        return;
    }
    std::vector<ScriptSelectorDialog::Entry> entries;
    try {
        entries = ScriptSelectorDialog::buildEntries(_resources);
    } catch (const std::exception& e) {
        spdlog::warn("ObjectScriptSection: scripts.lst unavailable: {}", e.what());
        return;
    }
    for (const auto& entry : entries) {
        auto* item = new QStandardItem(catalogText(entry));
        item->setData(entry.index, PROGRAM_INDEX_ROLE);
        _catalog->appendRow(item);
    }
}

void ObjectScriptSection::choose(int programIndex) {
    if (programIndex < 0 || programIndex == _programIndex) {
        showPickerText();
        return;
    }
    Q_EMIT attachRequested(programIndex);
}

void ObjectScriptSection::browse() {
    std::vector<ScriptSelectorDialog::Entry> entries;
    try {
        entries = ScriptSelectorDialog::buildEntries(_resources);
    } catch (const std::exception& e) {
        spdlog::warn("ObjectScriptSection: scripts.lst unavailable: {}", e.what());
    }
    ScriptSelectorDialog dialog(entries, _programIndex, this);
    if (dialog.exec() == QDialog::Accepted) {
        choose(dialog.selectedIndex());
    }
}

const MapScript* ObjectScriptSection::attachedScript() const {
    if (!_map || !_object || _object->map_scripts_pid == -1) {
        return nullptr;
    }
    const auto sid = static_cast<uint32_t>(_object->map_scripts_pid);
    for (const auto& section : _map->getMapFile().map_scripts) {
        for (const MapScript& script : section) {
            if (script.pid == sid) {
                return &script;
            }
        }
    }
    return nullptr;
}

bool ObjectScriptSection::isCritter() const {
    return _object && _object->objectType() == ObjectType::Critter;
}

void ObjectScriptSection::showObject(Map* map, std::shared_ptr<MapObject> object) {
    _map = map;
    _object = std::move(object);

    const MapScript* script = attachedScript();
    const std::optional<int> programIndex = (_map && _object && _object->map_scripts_pid != -1)
        ? _map->scriptProgramIndexForSid(static_cast<uint32_t>(_object->map_scripts_pid))
        : std::nullopt;
    _programIndex = programIndex.value_or(-1);

    const bool enabled = _map != nullptr && _object != nullptr;
    _picker->setEnabled(enabled);
    _browseButton->setEnabled(enabled);
    _moreButton->setEnabled(_programIndex >= 0);

    ScriptSourceService::ScriptStatus status;
    if (_programIndex >= 0) {
        ensureCatalog();
        _shownText = _programIndex < _catalog->rowCount()
            ? _catalog->item(_programIndex)->text()
            : QString("Script #%1").arg(_programIndex);
        if (_statusProvider) {
            status = _statusProvider(_programIndex);
        }
        // CLAUDE.md "Script Index Bases": the map stores the 0-based index, SSL headers name it 1-based.
        const QString type = QString::fromUtf8(MapScript::toString(
            MapScript::fromPid(static_cast<uint32_t>(_object->map_scripts_pid))));
        _identity->setFullText(QString("Program #%1 · SCRIPT_* %2 · %3 script")
                .arg(_programIndex)
                .arg(_programIndex + 1)
                .arg(type));
        _identity->setToolTip("The map stores the 0-based scripts.lst index (#" + QString::number(_programIndex)
            + "); headers/scripts.h names the same script with the 1-based SCRIPT_* constant ("
            + QString::number(_programIndex + 1) + ").");
    } else {
        _shownText.clear();
        _identity->setFullText({});
    }
    showPickerText();
    _identity->setVisible(_programIndex >= 0);
    _openSourceAction->setEnabled(_programIndex >= 0);
    _openSourceAction->setToolTip(status.source ? QString() : QString("No SSL source found for this script"));
    _showInPanelAction->setEnabled(script != nullptr);
    _detachAction->setEnabled(_object && _object->map_scripts_pid != -1);

    refreshDetails(script, status);
}

void ObjectScriptSection::refreshDetails(const MapScript* script, const ScriptSourceService::ScriptStatus& status) {
    QStringList warnings;
    QString hint;
    resource::ScriptSourceFacts facts;

    if (_programIndex >= 0 && _statusProvider) {
        if (!status.resolved) {
            warnings << QString("⚠ scripts.lst has no entry #%1.").arg(_programIndex);
        } else if (!status.compiled) {
            warnings << "⚠ Its compiled .int is not in the mounted data, so the game can't run it.";
        }
        if (status.source) {
            facts = resource::inspectScriptSource(*status.source);
        } else if (status.resolved) {
            hint = "No SSL source found. Mark a script source tree (e.g. the Restoration Project's "
                   "scripts_src) in Preferences › Data Paths to open it and check what it overrides.";
        }
    }

    // Team and AI packet are critter fields; the script's assignment replaces the map's value.
    if (isCritter()) {
        for (const auto& o : facts.overrides) {
            if (o.procedure == "map_enter_p_proc") {
                warnings << QString("⚠ Sets the %1 on map entry (map_enter_p_proc, line %2), replacing the "
                                    "one chosen in Edit Critter every time the map loads.")
                                .arg(fieldName(o.field))
                                .arg(o.line);
            } else {
                warnings << QString("Changes the %1 in %2 (line %3) while the game runs.")
                                .arg(fieldName(o.field), QString::fromStdString(o.procedure))
                                .arg(o.line);
            }
        }
    }

    _warnings->setText(warnings.join('\n'));
    _warnings->setVisible(!warnings.isEmpty());
    _hint->setText(hint);
    _hint->setVisible(!hint.isEmpty());
    refreshLocalVariables(script, facts);
}

void ObjectScriptSection::refreshLocalVariables(const MapScript* script, const resource::ScriptSourceFacts& facts) {
    _localVars->clear();
    const bool hasVars = _map && script && script->local_var_offset != MapScript::NONE && script->local_var_count > 0;
    _localVarsTitle->setVisible(hasVars);
    _localVars->setVisible(hasVars);
    if (!hasVars) {
        return;
    }
    const auto& values = _map->getMapFile().map_local_vars;
    for (uint32_t i = 0; i < script->local_var_count; ++i) {
        const std::size_t index = static_cast<std::size_t>(script->local_var_offset) + i;
        if (index >= values.size()) {
            break;
        }
        const auto name = facts.localVarNames.find(static_cast<int>(i));
        auto* row = new QTreeWidgetItem(_localVars);
        row->setText(0, name != facts.localVarNames.end() ? QString::fromStdString(name->second) : QString("LVAR %1").arg(i));
        row->setText(1, QString::number(values[index]));
        row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    const int rows = std::min(_localVars->topLevelItemCount(), LOCAL_VARS_MAX_ROWS);
    const int rowHeight = _localVars->sizeHintForRow(0) > 0 ? _localVars->sizeHintForRow(0) : fontMetrics().height() + 4;
    _localVars->setFixedHeight(_localVars->header()->sizeHint().height() + rows * rowHeight + 2 * _localVars->frameWidth());
}

} // namespace geck
