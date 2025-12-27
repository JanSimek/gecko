#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace geck {
namespace ui {
    namespace theme {

        /**
         * @brief Centralized color palette for consistent UI theming
         *
         * All colors used throughout the application should be defined here.
         * This enables future dark mode support and consistent visual design.
         */
        namespace colors {
            // Primary colors (selection, focus, accent)
            constexpr const char* PRIMARY = "#4A90E2";
            constexpr const char* PRIMARY_LIGHT = "#E6F2FF";
            constexpr const char* PRIMARY_DARK = "#0066CC";

            // Surface colors (backgrounds)
            constexpr const char* SURFACE = "#FFFFFF";
            constexpr const char* SURFACE_LIGHT = "#F9F9F9";
            constexpr const char* SURFACE_MEDIUM = "#F5F5F5";
            constexpr const char* SURFACE_DARK = "#F0F0F0";
            constexpr const char* SURFACE_HOVER = "#E0E0E0";

            // Border colors
            constexpr const char* BORDER = "#CCCCCC";
            constexpr const char* BORDER_LIGHT = "#E2E8F0";
            constexpr const char* BORDER_MEDIUM = "#D0D0D0";
            constexpr const char* BORDER_DARK = "gray";

            // Status colors
            constexpr const char* SUCCESS = "#4CAF50";
            constexpr const char* WARNING = "#F57C00";
            constexpr const char* STATUS_ERROR = "#D32F2F"; // Avoid Windows ERROR macro
            constexpr const char* INFO = "blue";

            // Text colors
            constexpr const char* TEXT_PRIMARY = "black";
            constexpr const char* TEXT_SECONDARY = "gray";
            constexpr const char* TEXT_MUTED = "#666666";

            // QColor constants for programmatic use (tree items, overlays, etc.)
            // Status colors (RGB for non-stylesheet contexts)
            inline QColor statusWarningRgb() { return QColor(245, 124, 0); }
            inline QColor statusSuccessRgb() { return QColor(56, 142, 60); }
            inline QColor statusInfoRgb() { return QColor(128, 128, 128); }

            // Selection highlight (semi-transparent green)
            inline QColor selectionHighlight() { return QColor(0, 255, 0, 100); }

            // Object category colors (for placeholder icons)
            inline QColor categoryItems() { return QColor(100, 150, 255); }
            inline QColor categoryScenery() { return QColor(100, 255, 100); }
            inline QColor categoryCritters() { return QColor(255, 150, 100); }
            inline QColor categoryWalls() { return QColor(150, 150, 150); }
            inline QColor categoryMisc() { return QColor(255, 100, 255); }

            // Quantity overlay text
            inline QColor quantityText() { return QColor(50, 255, 50); }

            // Invalid/error indicator
            inline QColor invalidPath() { return Qt::red; }

            // Basic text colors for painting
            inline QColor textDark() { return Qt::black; }
            inline QColor textLight() { return Qt::white; }

            // Primary color for selection/focus highlighting
            inline QColor primary() { return QColor(PRIMARY); }
        }

        /**
         * @brief Font constants for consistent typography
         */
        namespace fonts {
            // Small labels for overlays and compact displays
            // Note: Avoid 'small' - it's a Windows macro (rpcndr.h)
            inline QFont compactBold() { return QFont("Arial", 8, QFont::Bold); }
            inline QFont compact() { return QFont("Arial", 8); }
            inline QFont tiny() { return QFont("Arial", 6); }

            // Standard UI font (cross-platform friendly)
            inline QFont standard() { return QFont("Segoe UI", 9); }

            // Size-specific fonts for UI elements
            inline QFont small() { return QFont("Segoe UI", 10); }
            inline QFont title() { return QFont("Segoe UI", 14, QFont::Bold); }
            inline QFont largeTitle() { return QFont("Segoe UI", 16, QFont::DemiBold); }
            inline QFont statusText() { return QFont("Segoe UI", 11); }

            // Monospace for code/data display
            inline QFont monospace() { return QFont("Monaco, Consolas, 'Courier New', monospace", 10); }
            inline QFont monospaceBold() {
                QFont f("Monaco, Consolas, 'Courier New', monospace", 14);
                f.setBold(true);
                return f;
            }
        }

        /**
         * @brief Spacing constants for consistent layout
         */
        namespace spacing {
            constexpr int TIGHT = 4;    // Compact/nested layouts
            constexpr int NORMAL = 8;   // Standard widget spacing
            constexpr int LOOSE = 12;   // Dialog/group spacing
            constexpr int SECTION = 16; // Section separation

            // Margins
            constexpr int MARGIN_TIGHT = 4;
            constexpr int MARGIN_NORMAL = 8;
            constexpr int MARGIN_LOOSE = 12;
            constexpr int MARGIN_INDENT = 20;
        }

        /**
         * @brief Pre-built stylesheet strings for common UI patterns
         */
        namespace styles {
            // Selection states
            inline QString selectedWidget() {
                return QString("border: 2px solid %1; background-color: %2;")
                    .arg(colors::PRIMARY, colors::PRIMARY_LIGHT);
            }

            inline QString normalWidget() {
                return QString("border: 1px solid %1; background-color: %2;")
                    .arg(colors::BORDER_DARK, colors::SURFACE);
            }

            // Preview/display areas
            inline QString previewArea() {
                return QString("border: 1px solid %1; background-color: %2;")
                    .arg(colors::BORDER_DARK, colors::SURFACE_DARK);
            }

            // Input fields (read-only appearance)
            inline QString readOnlyInput() {
                return QString("QLineEdit { background-color: %1; }")
                    .arg(colors::SURFACE_LIGHT);
            }

            inline QString textAreaReadOnly() {
                return QString("QTextEdit { background-color: %1; border: 1px solid %2; margin: 0px; padding: 2px; }")
                    .arg(colors::SURFACE_LIGHT, colors::BORDER);
            }

            // Labels with borders (for FID displays, etc.)
            inline QString borderedLabel() {
                return QString("QLabel { border: 1px solid %1; padding: 2px 4px; background-color: %2; }")
                    .arg(colors::BORDER_LIGHT, colors::SURFACE);
            }

            // Status text styles
            inline QString statusNormal() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::TEXT_SECONDARY);
            }

            inline QString statusWarning() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::WARNING);
            }

            inline QString statusError() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::STATUS_ERROR);
            }

            inline QString statusSuccess() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::SUCCESS);
            }

            inline QString statusInfo() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::INFO);
            }

            // Help/small text
            inline QString helpText() {
                return QString("QLabel { color: %1; font-size: 11px; margin-bottom: 8px; }")
                    .arg(colors::TEXT_SECONDARY);
            }

            inline QString smallLabel() {
                return QString("QLabel { color: %1; font-size: 11px; }")
                    .arg(colors::TEXT_SECONDARY);
            }

            // Priority/emphasis labels
            inline QString emphasisLabel() {
                return QString("font-weight: bold; color: %1;")
                    .arg(colors::PRIMARY_DARK);
            }

            // Bold label (for table/grid headers)
            inline QString boldLabel() {
                return QString("font-weight: bold;");
            }

            // Placeholder/disabled text
            inline QString placeholderText() {
                return QString("color: %1;")
                    .arg(colors::TEXT_SECONDARY);
            }

            // Error text with monospace
            inline QString errorMonospace() {
                return QString("color: %1; font-family: monospace;")
                    .arg(colors::STATUS_ERROR);
            }

            inline QString warningMonospace() {
                return QString("color: %1; font-family: monospace;")
                    .arg(colors::WARNING);
            }

            // Normal monospace text (for script/code display)
            inline QString monospaceText() {
                return QString("color: %1; font-family: monospace;")
                    .arg(colors::TEXT_PRIMARY);
            }

            // Progress bar (loading widget)
            inline QString progressBar() {
                return QString("background-color: %1;")
                    .arg(colors::SUCCESS);
            }

            // Slider styling
            inline QString compactSlider() {
                return QString(
                    "QSlider::groove:horizontal {"
                    "  border: 1px solid %1;"
                    "  height: 4px;"
                    "  background: %2;"
                    "}"
                    "QSlider::handle:horizontal {"
                    "  background: %3;"
                    "  width: 12px;"
                    "  margin: -4px 0;"
                    "}"
                    "QSlider::handle:horizontal:hover {"
                    "  background: %4;"
                    "}")
                    .arg(colors::BORDER_MEDIUM, colors::SURFACE_MEDIUM,
                        colors::SURFACE_MEDIUM, colors::SURFACE_HOVER);
            }

            // Overlay button (circular, semi-transparent for preview widgets)
            inline QString overlayButton() {
                return QString(
                    "QPushButton {"
                    "  background-color: rgba(255, 255, 255, 180);"
                    "  border: 1px solid rgba(0, 0, 0, 100);"
                    "  border-radius: 12px;"
                    "}"
                    "QPushButton:hover {"
                    "  background-color: rgba(255, 255, 255, 220);"
                    "  border-color: rgba(0, 0, 0, 150);"
                    "}"
                    "QPushButton:pressed {"
                    "  background-color: rgba(255, 255, 255, 255);"
                    "}");
            }

            // FID/file selector button (clickable label-like appearance)
            inline QString fidButton() {
                return QString(
                    "QPushButton {"
                    "  border: 1px solid %1;"
                    "  padding: 2px 4px;"
                    "  background-color: %2;"
                    "  text-align: left;"
                    "}"
                    "QPushButton:hover {"
                    "  background-color: %3;"
                    "  border-color: #999;"
                    "}"
                    "QPushButton:pressed {"
                    "  background-color: %4;"
                    "}")
                    .arg(colors::BORDER_MEDIUM, colors::SURFACE,
                        colors::SURFACE_MEDIUM, colors::SURFACE_HOVER);
            }

            // Bold group box header style
            inline QString boldGroupBox() {
                return QString("QGroupBox { font-weight: bold; }");
            }

            // Progress bar style (loading dialogs)
            inline QString progressBarStyle() {
                return QString(
                    "QProgressBar {"
                    "  border: 1px solid %1;"
                    "  border-radius: 3px;"
                    "  text-align: center;"
                    "  height: 20px;"
                    "}"
                    "QProgressBar::chunk {"
                    "  background-color: %2;"
                    "  border-radius: 2px;"
                    "}")
                    .arg(colors::BORDER, colors::SUCCESS);
            }

            // Muted/secondary text (build info, help text)
            inline QString mutedText() {
                return QString("QLabel { color: %1; font-size: 10pt; }")
                    .arg(colors::TEXT_MUTED);
            }

            // Italic secondary text
            inline QString italicSecondaryText() {
                return QString("QLabel { color: %1; font-style: italic; }")
                    .arg(colors::TEXT_SECONDARY);
            }

            // Compact label (small, no margin/padding for tight layouts)
            inline QString compactLabel() {
                return QString("QLabel { font-size: 10px; margin: 0px; padding: 0px; }");
            }

            // Title/header label (bold, larger)
            inline QString titleLabel() {
                return QString("QLabel { font-weight: bold; font-size: 14px; padding: 4px; }");
            }

            // Compact widget (no margin/padding/border)
            inline QString compactWidget() {
                return QString("QWidget { margin: 0px; padding: 0px; border: none; }");
            }

            // Transparent background widget
            inline QString transparentWidget() {
                return QString("QLabel { background-color: transparent; }");
            }

            // Bold label with bottom margin (dialog titles)
            inline QString boldLabelWithMargin() {
                return QString("QLabel { font-weight: bold; margin-bottom: %1px; }")
                    .arg(spacing::MARGIN_TIGHT);
            }
        }

    } // namespace theme
} // namespace ui
} // namespace geck
