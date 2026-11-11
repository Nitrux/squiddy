import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.mauikit.controls as Maui

ColumnLayout {
    id: welcomeStep

    property bool advancedKeyboardVisible: false
    property bool moreLanguagesVisible: false
    property string lastFeaturedLanguage: ""

    readonly property var featuredLanguages: {
        const available = installationPlan.availableLanguages
        const locales = installationPlan.availableLocales
        const preferred = [
            { name: "English", code: "en", territory: "US" },
            { name: "Spanish", code: "es", territory: "ES" },
            { name: "French", code: "fr", territory: "FR" },
            { name: "German", code: "de", territory: "DE" },
            { name: "Japanese", code: "ja", territory: "JP" }
        ]
        const featured = []

        for (const preference of preferred) {
            for (let index = 0; index < available.length; ++index) {
                const language = available[index]
                if (language !== preference.name && !language.startsWith(preference.name + " ("))
                    continue

                const preferredLocale = preference.code + "_" + preference.territory
                let localeName = locales.includes(preferredLocale + ".UTF-8")
                    ? preferredLocale + ".UTF-8" : ""
                for (let localeIndex = 0; !localeName && localeIndex < locales.length; ++localeIndex) {
                    const candidate = locales[localeIndex]
                    if (candidate === preferredLocale || candidate.startsWith(preferredLocale + ".")) {
                        localeName = candidate
                        break
                    }
                }
                if (!localeName) {
                    for (let localeIndex = 0; localeIndex < locales.length; ++localeIndex) {
                        const candidate = locales[localeIndex]
                        if (candidate === preference.code || candidate.startsWith(preference.code + "_")) {
                            localeName = candidate
                            break
                        }
                    }
                }
                const locale = localeName ? Qt.locale(localeName) : null
                const nativeName = locale ? locale.nativeLanguageName : ""
                featured.push({
                    value: language,
                    locale: localeName,
                    title: nativeName ? nativeName.charAt(0).toLocaleUpperCase() + nativeName.slice(1) : language,
                    subtitle: locale ? locale.nativeTerritoryName : ""
                })
                break
            }
        }

        for (let index = 0; index < available.length && featured.length < preferred.length; ++index) {
            const language = available[index]
            if (!featured.some(choice => choice.value === language))
                featured.push({ value: language, locale: "", title: language, subtitle: "" })
        }
        return featured
    }

    function isFeaturedLanguage(language) {
        return featuredLanguages.some(choice => choice.value === language
                                      && (!choice.locale || choice.locale === installationPlan.locale))
    }

    Component.onCompleted: moreLanguagesVisible = !isFeaturedLanguage(installationPlan.language)

    Connections {
        target: installationPlan
        function onPlanChanged() {
            if (!installationPlan.regionalSettingsChangedByUser())
                welcomeStep.moreLanguagesVisible = !welcomeStep.isFeaturedLanguage(installationPlan.language)
        }
    }

    spacing: Maui.Style.space.big

    Maui.SectionHeader {
        Layout.fillWidth: true
        text1: qsTr("Select a Language")
        text2: qsTr("Choose your language, regional formats, time zone, and keyboard layout before continuing.")
        label1.font.weight: Font.Normal
        label1.font.pointSize: Maui.Style.fontSizes.enormous * 2
        label1.wrapMode: Text.WordWrap
        label2.wrapMode: Text.WordWrap
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Maui.Style.space.small

        Repeater {
            model: featuredLanguages

            delegate: CheckBox {
                id: languageChoice

                Layout.fillWidth: true
                LayoutMirroring.enabled: true
                enabled: !welcomeStep.moreLanguagesVisible
                padding: Maui.Style.contentMargins
                text: modelData.title
                implicitHeight: Math.max(contentItem.implicitHeight, indicator.implicitHeight)
                    + topPadding + bottomPadding
                Binding on checked {
                    value: !welcomeStep.moreLanguagesVisible
                        && installationPlan.language === modelData.value
                    restoreMode: Binding.RestoreBinding
                }
                onClicked: installationPlan.selectLanguage(modelData.value, modelData.locale)

                contentItem: ColumnLayout {
                    spacing: Maui.Style.space.small

                    Label {
                        Layout.fillWidth: true
                        Layout.rightMargin: languageChoice.indicator.implicitWidth + languageChoice.spacing
                        text: modelData.title
                        font: Maui.Style.h2Font
                        horizontalAlignment: Text.AlignLeft
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.rightMargin: languageChoice.indicator.implicitWidth + languageChoice.spacing
                        visible: modelData.subtitle.length > 0
                        text: modelData.subtitle
                        font.pointSize: Maui.Style.fontSizes.small
                        opacity: languageChoice.checked ? 0.8 : 0.6
                        horizontalAlignment: Text.AlignLeft
                    }
                }

                background: Rectangle {
                    radius: Maui.Style.radiusV
                    color: languageChoice.checked ? Maui.Theme.backgroundColor : Maui.Theme.alternateBackgroundColor
                    border.color: languageChoice.checked ? Maui.Theme.highlightColor : "transparent"
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: moreLanguagesCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: moreLanguagesCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("More languages")
                label2.text: welcomeStep.moreLanguagesVisible
                    ? qsTr("Selected language: %1").arg(installationPlan.language)
                    : qsTr("Choose from all available languages.")
                label2.wrapMode: Text.Wrap

                template.content: Switch {
                    checked: welcomeStep.moreLanguagesVisible
                    onToggled: {
                        const currentCard = welcomeStep.featuredLanguages.find(
                            choice => choice.value === installationPlan.language)
                        if (checked && currentCard)
                            welcomeStep.lastFeaturedLanguage = currentCard.value
                        if (!checked) {
                            const fallback = currentCard ? currentCard.value
                                : (welcomeStep.lastFeaturedLanguage
                                    || (welcomeStep.featuredLanguages.length > 0
                                        ? welcomeStep.featuredLanguages[0].value : ""))
                            if (fallback) {
                                const card = welcomeStep.featuredLanguages.find(choice => choice.value === fallback)
                                if (card && (!currentCard || (card.locale && card.locale !== installationPlan.locale)))
                                    installationPlan.selectLanguage(fallback, card.locale)
                            }
                        }
                        welcomeStep.moreLanguagesVisible = checked
                    }
                }
            }

            Button {
                visible: welcomeStep.moreLanguagesVisible
                text: qsTr("Choose a language")
                onClicked: languageDialog.open()
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: localeCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: localeCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Locale and regional formats")
                text2: qsTr("Choose the locale for the installed system.")
            }

            ComboBox {
                Layout.fillWidth: true
                model: installationPlan.availableLocales
                currentIndex: installationPlan.availableLocales.indexOf(installationPlan.locale)
                displayText: currentIndex >= 0 ? currentText : qsTr("No supported locales were found on the live system.")
                onActivated: {
                    installationPlan.markRegionalSettingsChanged()
                    installationPlan.locale = currentText
                }
                popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 8)
                popupScrollBar: true
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: timeZoneCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: timeZoneCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                uniformCellWidths: true
                columnSpacing: Maui.Style.space.medium
                rowSpacing: Maui.Style.space.small

                Maui.SectionHeader {
                    Layout.fillWidth: true
                    text1: qsTr("Country")
                    text2: qsTr("Choose the time zone region for the installed system.")
                }

                Maui.SectionHeader {
                    Layout.fillWidth: true
                    text1: qsTr("City")
                    text2: qsTr("Choose a location or time zone in the selected region.")
                }

                ComboBox {
                    id: timeZoneRegionCombo
                    Layout.fillWidth: true
                    model: installationPlan.timeZoneRegionOptions
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: {
                        for (let index = 0; index < model.length; ++index) {
                            if (model[index].value === installationPlan.timeZoneRegion)
                                return index
                        }
                        return -1
                    }
                    displayText: currentIndex >= 0 ? currentText : qsTr("No time zone regions were found.")
                    onActivated: {
                        if (currentValue !== installationPlan.timeZoneRegion) {
                            const zones = installationPlan.timeZoneOptionsForRegion(currentValue)
                            if (zones.length > 0) {
                                installationPlan.markRegionalSettingsChanged()
                                installationPlan.timeZone = zones[0].value
                            }
                        }
                    }
                    popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 8)
                    popupScrollBar: true
                    delegate: MenuItem {
                        width: ListView.view.width
                        text: timeZoneRegionCombo.model[index].text
                        highlighted: timeZoneRegionCombo.highlightedIndex === index
                    }
                }

                ComboBox {
                    id: timeZoneCityCombo
                    Layout.fillWidth: true
                    model: installationPlan.timeZoneOptionsForRegion(installationPlan.timeZoneRegion)
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: {
                        for (let index = 0; index < model.length; ++index) {
                            if (model[index].value === installationPlan.timeZone)
                                return index
                        }
                        return -1
                    }
                    displayText: currentIndex >= 0 ? currentText : qsTr("No time zones were found in this region.")
                    onActivated: {
                        installationPlan.markRegionalSettingsChanged()
                        installationPlan.timeZone = currentValue
                    }
                    popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 8)
                    popupScrollBar: true
                    delegate: MenuItem {
                        width: ListView.view.width
                        text: timeZoneCityCombo.model[index].text
                        highlighted: timeZoneCityCombo.highlightedIndex === index
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        color: Maui.Theme.alternateBackgroundColor
        radius: Maui.Style.radiusV
        border.color: Maui.Theme.backgroundColor
        implicitHeight: keyboardCardLayout.implicitHeight + Maui.Style.contentMargins * 2

        ColumnLayout {
            id: keyboardCardLayout
            anchors.fill: parent
            anchors.margins: Maui.Style.contentMargins
            spacing: Maui.Style.space.small

            Maui.SectionHeader {
                Layout.fillWidth: true
                text1: qsTr("Keyboard layout")
                text2: qsTr("Choose the layout for the installed system.")
            }

            ComboBox {
                id: keyboardLayoutCombo
                Layout.fillWidth: true
                model: installationPlan.keyboardLayoutOptions
                textRole: "text"
                valueRole: "value"
                currentIndex: installationPlan.availableKeyboardLayouts.indexOf(installationPlan.keyboardLayout)
                displayText: currentIndex >= 0 ? currentText : qsTr("No keyboard layouts were found on the live system.")
                onActivated: {
                    installationPlan.markRegionalSettingsChanged()
                    installationPlan.keyboardLayout = currentValue
                }
                popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 8)
                popupScrollBar: true
                delegate: MenuItem {
                    width: ListView.view.width
                    text: installationPlan.keyboardLayoutOptions[index].text
                    highlighted: keyboardLayoutCombo.highlightedIndex === index
                }
            }

            Maui.FlexSectionItem {
                Layout.fillWidth: true
                flat: true
                label1.text: qsTr("Advanced keyboard configuration")
                label2.text: qsTr("Choose an optional variant, model, or keyboard options.")
                label2.wrapMode: Text.Wrap

                template.content: Switch {
                    checked: welcomeStep.advancedKeyboardVisible
                    onToggled: welcomeStep.advancedKeyboardVisible = checked
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: welcomeStep.advancedKeyboardVisible
        spacing: Maui.Style.space.big

        Maui.FlexSectionItem {
            Layout.fillWidth: true
            flat: true
            label1.text: qsTr("Variant")
            label2.text: qsTr("Choose a variation of the selected keyboard layout.")
            label2.wrapMode: Text.Wrap

            ComboBox {
                id: keyboardVariantCombo
                Layout.fillWidth: true
                model: [{ text: qsTr("Default"), value: "" }]
                    .concat(installationPlan.keyboardVariantOptions)
                textRole: "text"
                valueRole: "value"
                currentIndex: model.findIndex(choice => choice.value === installationPlan.keyboardVariant)
                onActivated: {
                    installationPlan.markRegionalSettingsChanged()
                    installationPlan.keyboardVariant = currentValue
                }
                popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 8)
                popupScrollBar: true
                delegate: MenuItem {
                    width: ListView.view.width
                    text: keyboardVariantCombo.model[index].text
                    highlighted: keyboardVariantCombo.highlightedIndex === index
                }
            }
        }

        Maui.FlexSectionItem {
            Layout.fillWidth: true
            flat: true
            label1.text: qsTr("Model")
            label2.text: qsTr("Choose a keyboard model if the default does not match your hardware.")
            label2.wrapMode: Text.Wrap

            ComboBox {
                id: keyboardModelCombo
                Layout.fillWidth: true
                model: [{ text: qsTr("Default"), value: "" }]
                    .concat(installationPlan.keyboardModelOptions)
                textRole: "text"
                valueRole: "value"
                currentIndex: model.findIndex(choice => choice.value === installationPlan.keyboardModel)
                onActivated: {
                    installationPlan.markRegionalSettingsChanged()
                    installationPlan.keyboardModel = currentValue
                }
                popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 8)
                popupScrollBar: true
                delegate: MenuItem {
                    width: ListView.view.width
                    text: keyboardModelCombo.model[index].text
                    highlighted: keyboardModelCombo.highlightedIndex === index
                }
            }
        }

        Maui.FlexSectionItem {
            Layout.fillWidth: true
            flat: true
            label1.text: qsTr("Options")
            label2.text: qsTr("Select additional keyboard behaviors.")
            label2.wrapMode: Text.Wrap

            ComboBox {
                id: keyboardOptionsCombo
                Layout.fillWidth: true
                model: [{ text: qsTr("None"), value: "" }]
                    .concat(installationPlan.keyboardOptionOptions)
                textRole: "text"
                valueRole: "value"
                displayText: {
                    const selected = installationPlan.keyboardOptions.split(",").filter(code => code.length > 0)
                    if (selected.length === 0)
                        return qsTr("None")
                    if (selected.length > 1)
                        return qsTr("%1 options selected").arg(selected.length)
                    const choice = installationPlan.keyboardOptionOptions.find(item => item.value === selected[0])
                    return choice ? choice.text : selected[0]
                }
                onActivated: function(index) {
                    installationPlan.markRegionalSettingsChanged()
                    const choice = model[index].value
                    if (!choice) {
                        installationPlan.keyboardOptions = ""
                        return
                    }
                    const selected = installationPlan.keyboardOptions.split(",").filter(code => code.length > 0)
                    const position = selected.indexOf(choice)
                    if (position >= 0)
                        selected.splice(position, 1)
                    else
                        selected.push(choice)
                    installationPlan.keyboardOptions = selected.join(",")
                }
                popup.height: Math.min(popup.implicitHeight, Maui.Style.rowHeight * 8)
                popupScrollBar: true
                delegate: MenuItem {
                    width: ListView.view.width
                    text: keyboardOptionsCombo.model[index].text
                    icon.name: {
                        const choice = keyboardOptionsCombo.model[index].value
                        const selected = installationPlan.keyboardOptions.split(",")
                            .filter(code => code.length > 0)
                        return (choice ? selected.includes(choice) : selected.length === 0)
                            ? "emblem-default" : ""
                    }
                    highlighted: keyboardOptionsCombo.highlightedIndex === index
                }
            }
        }
    }

    Maui.PopupPage {
        id: languageDialog

        title: qsTr("Additional Languages")
        maxHeight: parent ? parent.height - Maui.Style.contentMargins * 2 : implicitHeight
        maxWidth: Maui.Style.units.gridUnit * 28

        stack: Maui.ListBrowser {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: installationPlan.availableLanguages
            currentIndex: installationPlan.availableLanguages.indexOf(installationPlan.language)

            delegate: Maui.ListDelegate {
                width: ListView.view.width
                iconVisible: false
                label: modelData
                isCurrentItem: installationPlan.language === modelData
                onClicked: {
                    installationPlan.selectLanguage(modelData, "")
                    languageDialog.close()
                }
            }
        }
    }
}
