#include "WebSettingsSchemaValidator.h"

namespace hp {
namespace {

bool validId(const String& value, size_t maxLen = 48) {
    if (!value.length() || value.length() > maxLen) return false;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    }
    return true;
}

bool validText(const String& value, size_t maxLen) {
    if (!value.length() || value.length() > maxLen) return false;
    for (size_t i = 0; i < value.length(); ++i)
        if ((unsigned char)value[i] < 0x20 && value[i] != '\t') return false;
    return true;
}

bool validSimpleType(const String& type) {
    return type == "text" || type == "password" || type == "bool" ||
           type == "integer" || type == "number" || type == "select" ||
           type == "host" || type == "url";
}

bool validateOptions(JsonObjectConst field, String& error) {
    if (!field["options"].is<JsonArrayConst>() || field["options"].size() < 1 ||
        field["options"].size() > WebSettingsSchemaValidator::kMaxOptions) {
        error = "web_settings_options"; return false;
    }
    String seen[WebSettingsSchemaValidator::kMaxOptions];
    size_t count = 0;
    for (JsonVariantConst option : field["options"].as<JsonArrayConst>()) {
        String value, label;
        if (option.is<const char*>()) {
            value = option.as<String>(); label = value;
        } else if (option.is<JsonObjectConst>()) {
            const auto object = option.as<JsonObjectConst>();
            if (!object["value"].is<const char*>() || !object["label"].is<const char*>()) {
                error = "web_settings_option_shape"; return false;
            }
            value = object["value"].as<String>();
            label = object["label"].as<String>();
        } else {
            error = "web_settings_option_shape"; return false;
        }
        if (!validText(value, 96) || !validText(label, 96)) {
            error = "web_settings_option_text"; return false;
        }
        for (size_t i = 0; i < count; ++i)
            if (seen[i] == value) { error = "web_settings_duplicate_option"; return false; }
        seen[count++] = value;
    }
    return true;
}

bool validateSimple(JsonObjectConst field, String& error, bool nested) {
    if(!field["discovery"].isUnbound()){error="web_settings_discovery_repeater_only";return false;}
    if (!field["key"].is<const char*>() || !field["label"].is<const char*>() ||
        !field["type"].is<const char*>()) {
        error = "web_settings_field_required"; return false;
    }
    const String key = field["key"].as<String>();
    const String label = field["label"].as<String>();
    const String type = field["type"].as<String>();
    if (!validId(key) || !validText(label, 72) || !validSimpleType(type)) {
        error = "web_settings_field_invalid"; return false;
    }
    if (nested && type == "password") {
        error = "web_settings_repeater_password"; return false;
    }
    if (!field["help"].isUnbound() &&
        (!field["help"].is<const char*>() || !validText(field["help"].as<String>(), 220))) {
        error = "web_settings_help"; return false;
    }
    if (!field["placeholder"].isUnbound() &&
        (!field["placeholder"].is<const char*>() ||
         field["placeholder"].as<String>().length() > 120)) {
        error = "web_settings_placeholder"; return false;
    }
    if (!field["maxLength"].isUnbound() &&
        (!field["maxLength"].is<uint16_t>() || field["maxLength"].as<uint16_t>() < 1 ||
         field["maxLength"].as<uint16_t>() > 2048)) {
        error = "web_settings_max_length"; return false;
    }
    if (type == "select" && !validateOptions(field, error)) return false;
    return true;
}

bool validateRepeater(JsonObjectConst field, String& error, size_t& totalFields) {
    if (!field["key"].is<const char*>() || !field["label"].is<const char*>() ||
        field["type"].as<String>() != "repeater" || !field["fields"].is<JsonArrayConst>()) {
        error = "web_settings_repeater_shape"; return false;
    }
    const String parentKey = field["key"].as<String>();
    if (!validId(parentKey) || !validText(field["label"].as<String>(), 72)) {
        error = "web_settings_repeater_invalid"; return false;
    }
    const auto children = field["fields"].as<JsonArrayConst>();
    if (children.size() < 1 || children.size() > WebSettingsSchemaValidator::kMaxRepeaterFields) {
        error = "web_settings_repeater_fields"; return false;
    }
    const uint8_t maxItems = field["maxItems"] | 8;
    if (maxItems < 1 || maxItems > WebSettingsSchemaValidator::kMaxRepeaterItems) {
        error = "web_settings_repeater_items"; return false;
    }
    String childKeys[WebSettingsSchemaValidator::kMaxRepeaterFields];
    size_t childCount = 0;
    for (JsonObjectConst child : children) {
        if (!validateSimple(child, error, true)) return false;
        const String key = child["key"].as<String>();
        if (key == parentKey) { error = "web_settings_repeater_key_collision"; return false; }
        for (size_t i = 0; i < childCount; ++i)
            if (childKeys[i] == key) { error = "web_settings_duplicate_repeater_key"; return false; }
        childKeys[childCount++] = key;
        if (++totalFields > WebSettingsSchemaValidator::kMaxTotalFields) {
            error = "web_settings_total_fields"; return false;
        }
    }
    if(!field["discovery"].isUnbound()){
        const auto discovery=field["discovery"].as<JsonObjectConst>();
        const auto mapping=discovery["map"].as<JsonObjectConst>();
        if(discovery.isNull()||discovery["type"].as<String>()!="printers"||mapping.isNull()||mapping.size()<1||mapping.size()>4){error="web_settings_discovery_shape";return false;}
        if(!mapping["host"].is<const char*>()){error="web_settings_discovery_host";return false;}
        String mapped[4];size_t n=0;
        for(JsonPairConst pair:mapping){
            const String source=pair.key().c_str(),target=pair.value().as<String>();
            if((source!="host"&&source!="hostname"&&source!="port"&&source!="protocol")||!pair.value().is<const char*>()||!validId(target)){error="web_settings_discovery_mapping";return false;}
            for(size_t i=0;i<n;++i)if(mapped[i]==target){error="web_settings_discovery_mapping";return false;}
            mapped[n++]=target;bool found=false;
            for(JsonObjectConst child:children)if(child["key"].as<String>()==target){
                const String type=child["type"].as<String>();
                found=source=="port"?(type=="integer"||type=="number"):(source=="host"?(type=="host"||type=="text"||type=="url"):(type=="text"||type=="select"));break;
            }
            if(!found){error="web_settings_discovery_mapping";return false;}
        }
        if(!discovery["protocolValues"].isUnbound()){
            if(!discovery["protocolValues"].is<JsonObjectConst>()||discovery["protocolValues"].size()>3){error="web_settings_discovery_protocols";return false;}
            for(JsonPairConst pair:discovery["protocolValues"].as<JsonObjectConst>()){
                const String name=pair.key().c_str();
                if((name!="moonraker"&&name!="rrf"&&name!="octoprint")||!pair.value().is<const char*>()||!validText(pair.value().as<String>(),96)){error="web_settings_discovery_protocols";return false;}
            }
        }
    }
    return true;
}

} // namespace

bool WebSettingsSchemaValidator::validate(JsonDocument& schema, String& error) {
    error = "";
    if (!schema["schema"].is<uint16_t>() || schema["schema"].as<uint16_t>() != 1) {
        error = "web_settings_schema"; return false;
    }
    if (!schema["tabs"].is<JsonArrayConst>() || schema["tabs"].size() < 1 ||
        schema["tabs"].size() > kMaxTabs) {
        error = "web_settings_tabs"; return false;
    }

    String tabIds[kMaxTabs];
    String topKeys[kMaxTopFields];
    size_t tabCount = 0, topCount = 0, totalFields = 0;

    for (JsonObjectConst tab : schema["tabs"].as<JsonArrayConst>()) {
        if (!tab["id"].is<const char*>() || !tab["label"].is<const char*>() ||
            !tab["fields"].is<JsonArrayConst>()) {
            error = "web_settings_tab_shape"; return false;
        }
        const String tabId = tab["id"].as<String>();
        if (!validId(tabId) || !validText(tab["label"].as<String>(), 48)) {
            error = "web_settings_tab_invalid"; return false;
        }
        for (size_t i = 0; i < tabCount; ++i)
            if (tabIds[i] == tabId) { error = "web_settings_duplicate_tab"; return false; }
        tabIds[tabCount++] = tabId;

        const auto fields = tab["fields"].as<JsonArrayConst>();
        if (fields.size() > kMaxTopFields || topCount + fields.size() > kMaxTopFields) {
            error = "web_settings_top_fields"; return false;
        }
        for (JsonObjectConst field : fields) {
            if (!field["key"].is<const char*>()) { error = "web_settings_field_required"; return false; }
            const String key = field["key"].as<String>();
            for (size_t i = 0; i < topCount; ++i)
                if (topKeys[i] == key) { error = "web_settings_duplicate_key"; return false; }
            topKeys[topCount++] = key;
            if (++totalFields > kMaxTotalFields) { error = "web_settings_total_fields"; return false; }

            if (field["type"].as<String>() == "repeater") {
                if (!validateRepeater(field, error, totalFields)) return false;
            } else if (!validateSimple(field, error, false)) {
                return false;
            }
        }
    }
    return true;
}

String WebSettingsSchemaValidator::message(const String& code) {
    if(code.startsWith("web_settings_discovery_"))return "web_settings.json: Discovery benötigt einen Repeater, type=printers und gültige Zuordnungen in map (host, hostname, port, protocol)";
    if (code == "web_settings_schema") return "web_settings.json: schema muss 1 sein";
    if (code == "web_settings_tabs") return "web_settings.json: 1 bis 12 Tabs erforderlich";
    if (code == "web_settings_tab_shape") return "web_settings.json: Tab benötigt id, label und fields";
    if (code == "web_settings_tab_invalid") return "web_settings.json: Tab-ID oder Beschriftung ist ungültig";
    if (code == "web_settings_duplicate_tab") return "web_settings.json: Tab-ID ist doppelt vorhanden";
    if (code == "web_settings_top_fields") return "web_settings.json: maximal 48 Hauptfelder erlaubt";
    if (code == "web_settings_total_fields") return "web_settings.json: insgesamt zu viele Felder (maximal 96 inklusive Repeater)";
    if (code == "web_settings_duplicate_key") return "web_settings.json: Einstellungs-Key ist doppelt vorhanden";
    if (code == "web_settings_field_required") return "web_settings.json: Feld benötigt key, label und type";
    if (code == "web_settings_field_invalid") return "web_settings.json: Feld-Key, Beschriftung oder Typ ist ungültig";
    if (code == "web_settings_help") return "web_settings.json: Hilfetext ist ungültig oder zu lang";
    if (code == "web_settings_placeholder") return "web_settings.json: Platzhalter ist zu lang";
    if (code == "web_settings_max_length") return "web_settings.json: maxLength muss zwischen 1 und 2048 liegen";
    if (code == "web_settings_options") return "web_settings.json: Auswahlfeld benötigt 1 bis 24 Optionen";
    if (code == "web_settings_option_shape") return "web_settings.json: Auswahloption ist ungültig";
    if (code == "web_settings_option_text") return "web_settings.json: Auswahlwert oder Beschriftung ist ungültig";
    if (code == "web_settings_duplicate_option") return "web_settings.json: Auswahlwert ist doppelt vorhanden";
    if (code == "web_settings_repeater_shape") return "web_settings.json: Repeater benötigt key, label, type=repeater und fields";
    if (code == "web_settings_repeater_invalid") return "web_settings.json: Repeater-Key oder Beschriftung ist ungültig";
    if (code == "web_settings_repeater_fields") return "web_settings.json: Repeater benötigt 1 bis 16 Unterfelder";
    if (code == "web_settings_repeater_items") return "web_settings.json: Repeater maxItems muss zwischen 1 und 16 liegen";
    if (code == "web_settings_repeater_password") return "web_settings.json: Passwortfelder sind innerhalb eines Repeaters nicht erlaubt";
    if (code == "web_settings_repeater_key_collision") return "web_settings.json: Repeater-Key darf nicht zugleich als Unterfeld verwendet werden";
    if (code == "web_settings_duplicate_repeater_key") return "web_settings.json: Repeater-Unterfeld-Key ist doppelt vorhanden";
    return code;
}

} // namespace hp
