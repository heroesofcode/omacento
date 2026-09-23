import QtQuick
import Quickshell.Io
import qs.Commons
import qs.Ui
import "Model.js" as Model

// Omacento — bar button plus a panel for macOS-style long-press accents.
//
// The panel never writes configuration. It persists into its inline shell.json
// entry and shells out to omacento-apply, which owns all four files the
// feature needs, so there is one place a setting can be wrong instead of four.
Panel {
  id: root
  moduleName: "io.github.heroesofcode.omacento"
  ipcTarget: "io.github.heroesofcode.omacento"

  readonly property string scriptPath: Qt.resolvedUrl("omacento-apply").toString().replace("file://", "")
  readonly property string appsPath: Qt.resolvedUrl("omacento-apps").toString().replace("file://", "")

  readonly property bool accentsOn: get("enabled") === true
  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color dim: Qt.darker(foreground, 1.45)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  property bool dropdownOwnsKeys: false

  // Effective value for a settings key: whatever the shell persisted, else the
  // plugin's own default. Every read goes through here so an unsaved key never
  // reads as empty and silently writes a half-configured fcitx5.
  function get(key) { return root.setting(key, Model.defaultFor(key)) }

  function save(patch) {
    root.settings = Object.assign({}, root.settings, patch)
    if (root.bar && root.bar.shell) root.bar.shell.updateEntryInline(root.moduleName, root.settings)
    applyDebounce.restart()
  }

  function apply() {
    if (applyProc.running) { applyDebounce.restart(); return }
    applyProc.command = Model.commandFor(root.scriptPath, root.get)
    applyProc.running = true
  }

  // Debounced because omacento-apply reloads fcitx5 and Hyprland, and dragging
  // the delay slider would otherwise fire one reload per pixel.
  Timer {
    id: applyDebounce
    interval: 350
    onTriggered: root.apply()
  }

  Process { id: applyProc }

  // Regenerate whenever the persisted settings change, including the moment
  // the shell first hands them over: a freshly enabled widget starts with an
  // empty object, so applying only at Component.onCompleted would write the
  // defaults and never revisit them.
  onSettingsChanged: applyDebounce.restart()

  // A fresh install, or a config file removed by something else, heals itself
  // without the user ever opening the panel.
  Component.onCompleted: applyDebounce.restart()

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "ã"
    tooltipText: Model.summary(root.get)
    onPressed: function(b) {
      if (b === Qt.RightButton) root.save({ enabled: !root.accentsOn })
      else root.toggle()
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(440))
    contentHeight: panel.fittedContentHeight(column.implicitHeight)

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      blocked: root.dropdownOwnsKeys
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height

        Column {
          id: column
          width: scroll.width
          spacing: Style.space(14)

          // ---------- Hero ----------
          Item {
            width: parent.width
            implicitHeight: Math.max(heroIcon.implicitHeight, heroLabels.implicitHeight, heroSwitch.implicitHeight)

            Text {
              id: heroIcon
              text: "ã"
              color: root.accentsOn ? root.foreground : root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.display
              anchors.left: parent.left
              anchors.verticalCenter: parent.verticalCenter

              Behavior on color { ColorAnimation { duration: 200 } }
            }

            Column {
              id: heroLabels
              anchors.left: heroIcon.right
              anchors.leftMargin: Style.space(14)
              anchors.right: heroSwitch.left
              anchors.rightMargin: Style.space(10)
              anchors.verticalCenter: parent.verticalCenter
              spacing: Style.space(2)

              Text {
                text: "Omacento"
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.title
                font.bold: true
                elide: Text.ElideRight
                width: parent.width
              }

              Text {
                text: Model.summary(root.get).toUpperCase()
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                font.bold: true
                font.letterSpacing: 1.2
                elide: Text.ElideRight
                width: parent.width
              }
            }

            ToggleSwitch {
              id: heroSwitch
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              checked: root.accentsOn
              foreground: root.foreground
              onToggled: root.save({ enabled: !root.accentsOn })
            }
          }

          PanelSeparator { foreground: root.foreground }

          // ---------- Behaviour ----------
          Column {
            width: parent.width
            spacing: Style.space(10)
            opacity: root.accentsOn ? 1.0 : 0.45

            Behavior on opacity { NumberAnimation { duration: 160 } }

            PanelSectionHeader {
              text: "BEHAVIOUR"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            SettingDropdown {
              width: (parent.width - Style.space(18)) / 2
              label: "Accent set"
              actionOptions: Model.LANGUAGES
              boundValue: root.get("language")
              onPicked: function(v) { root.save({ language: v }) }
            }

            Text {
              width: parent.width
              text: "Every set reaches the same characters — what changes is the order, and the order is what you actually feel. Português puts ã second on \"a\"; Français puts à first."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              wrapMode: Text.WordWrap
            }

            Item {
              width: parent.width
              implicitHeight: delayLabel.implicitHeight + Style.space(6) + delaySlider.implicitHeight

              Text {
                id: delayLabel
                anchors.left: parent.left
                text: "Hold before the popup"
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
              }

              Text {
                anchors.right: parent.right
                anchors.baseline: delayLabel.baseline
                text: Math.round(delaySlider.liveValue) + " ms"
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
              }

              PanelSlider {
                id: delaySlider
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: delayLabel.bottom
                anchors.topMargin: Style.space(6)
                bar: root.bar
                minimum: 100
                maximum: 1000
                step: 25
                integer: true
                value: root.get("holdTime")
                // Save on release only, so a drag does not rewrite the addon
                // config on every frame.
                onReleased: function(v) { root.save({ holdTime: Math.round(v) }) }
              }
            }

            Text {
              width: parent.width
              text: "The addon times this itself, so your keyboard repeat settings are left alone. Below about 200 ms it can fire while you are simply typing fast; macOS sits near 500."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              wrapMode: Text.WordWrap
            }
          }

          PanelSeparator { foreground: root.foreground }

          // ---------- Appearance ----------
          Column {
            width: parent.width
            spacing: Style.space(10)

            PanelSectionHeader {
              text: "APPEARANCE"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            Toggle {
              width: parent.width
              label: "Follow the Omarchy theme"
              description: "Rebuild the popup's colours whenever you switch themes."
              checked: root.get("themeSync") === true
              foreground: root.foreground
              fontFamily: root.fontFamily
              onClicked: root.save({ themeSync: root.get("themeSync") !== true })
            }

            Toggle {
              width: parent.width
              label: "Stack candidates vertically"
              description: "One accent per row instead of a single line."
              checked: root.get("vertical") === true
              foreground: root.foreground
              fontFamily: root.fontFamily
              onClicked: root.save({ vertical: root.get("vertical") !== true })
            }

            Item {
              width: parent.width
              implicitHeight: fontLabel.implicitHeight + Style.space(6) + fontSlider.implicitHeight

              Text {
                id: fontLabel
                anchors.left: parent.left
                text: "Popup text size"
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
              }

              Text {
                anchors.right: parent.right
                anchors.baseline: fontLabel.baseline
                text: Math.round(fontSlider.liveValue) + " pt"
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
              }

              PanelSlider {
                id: fontSlider
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: fontLabel.bottom
                anchors.topMargin: Style.space(6)
                bar: root.bar
                minimum: 10
                maximum: 28
                step: 1
                integer: true
                value: root.get("fontSize")
                onReleased: function(v) { root.save({ fontSize: Math.round(v) }) }
              }
            }

            Text {
              width: parent.width
              text: "The family is inherited from your terminal font, so the accents look like the text they are replacing."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              wrapMode: Text.WordWrap
            }
          }

          PanelSeparator { foreground: root.foreground }

          // ---------- Blocklist ----------
          Column {
            width: parent.width
            spacing: Style.space(10)

            PanelSectionHeader {
              text: "TURNED OFF IN"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            MultiSelect {
              id: blocklist
              width: parent.width
              label: "Applications"
              noSelectionText: "Everywhere"
              placeholderText: "Search open applications..."
              values: root.get("blocklist") || []
              optionsCommand: ["bash", root.appsPath]
              foreground: root.foreground
              fontFamily: root.fontFamily
              onChanged: function(v) { root.save({ blocklist: v }) }
              onPopupOpenChanged: root.dropdownOwnsKeys = popupOpen
            }

            Text {
              width: parent.width
              text: "Listed by window class, which is what fcitx5 reports as the program name — that is \"brave-browser\". Useful where holding a key should repeat it instead of opening the popup, like a game, or where the popup misbehaves."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              wrapMode: Text.WordWrap
            }
          }
        }
      }
    }
  }

  // Dropdown that reports its popup state, so the panel's key catcher stops
  // competing for j/k while a list is open.
  //
  // `value` is driven from `boundValue` through a handler rather than bound
  // straight to it: Dropdown assigns to its own `value` when a row is chosen,
  // which would destroy a declarative binding and leave the control deaf to
  // any later change made elsewhere.
  component SettingDropdown: Dropdown {
    property string boundValue: ""
    property var actionOptions: []

    signal picked(string value)

    options: actionOptions
    value: boundValue

    onBoundValueChanged: if (value !== boundValue) value = boundValue
    onChanged: function(v) { picked(v) }
    onPopupOpenChanged: root.dropdownOwnsKeys = popupOpen
  }
}
