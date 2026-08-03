/*
5.0
Transmogrification 3.3.5a - Gossip menu
By Rochet2

ScriptName for NPC:
Creature_Transmogrify

TODO:
Make DB saving even better (Deleting)? What about coding?

Fix the cost formula
-- Too much data handling, use default costs

Are the qualities right?
Blizzard might have changed the quality requirements.
(TC handles it with stat checks)

Cant transmogrify rediculus items // Foereaper: would be fun to stab people with a fish
-- Cant think of any good way to handle this easily, could rip flagged items from cata DB
*/
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include "Transmogrification.h"
#include "Chat.h"
#include "ScriptedCreature.h"
#include "ItemTemplate.h"
#include "DatabaseEnv.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "PlayerGossip.h"
#include "PlayerGossipMgr.h"
#include <any>

#define sT  sTransmogrification
#define GTS session->GetAcoreString // dropped translation support, no one using?


static constexpr int32 TRANSMOG_GOSSIP_EXTENDED_BASE = 1001;
static inline uint32 EncodeTransmogCodeSender(uint32 sender) { return sender + TRANSMOG_GOSSIP_EXTENDED_BASE; }
static inline uint32 DecodeTransmogCodeSender(uint32 sender) { return sender >= uint32(TRANSMOG_GOSSIP_EXTENDED_BASE) ? sender - TRANSMOG_GOSSIP_EXTENDED_BASE : sender; }

static constexpr uint32 RTG_SCOREBOARD_MENU_ID = 10000;
// PlayerGossip_Scoreboard sender IDs are stable and intentionally preserved.
static constexpr uint32 RTG_SCOREBOARD_COSMETICS_SENDER = 152;
static constexpr uint32 TRANSMOG_SCOREBOARD_RETURN_SENDER = 250;
static constexpr uint32 TRANSMOG_OUTFIT_REVIEW_SENDER = 240;
static constexpr uint32 TRANSMOG_OUTFIT_APPLY_SENDER = 241;
static constexpr uint32 TRANSMOG_OUTFIT_CLEAR_SENDER = 242;
static constexpr uint32 TRANSMOG_TROPHY_SENDER = 243;
static constexpr uint32 TRANSMOG_OUTFIT_UPDATE_SENDER = 244;
static constexpr uint32 TRANSMOG_TROPHY_SOURCE_SENDER = 245;
static constexpr uint32 TRANSMOG_OUTFIT_CLEAN_SENDER = 246;
static constexpr uint32 TRANSMOG_TROPHY_PAGE_SIZE = 20;

static std::string FormatTrophyDate(uint32 timestamp)
{
    if (!timestamp) return {};
    std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm result{};
#if defined(_WIN32)
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    char buffer[16] = {};
    if (!std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &result))
        return {};
    return buffer;
}

static void OpenRTGScoreboardCosmetics(Player* player)
{
    if (!player || !player->GetSession())
        return;

    player->PlayerTalkClass->ClearMenus();
    CloseGossipMenuFor(player);
    sPlayerGossipMgr->ShowGossipMenu(player, RTG_SCOREBOARD_MENU_ID, RTG_SCOREBOARD_COSMETICS_SENDER, 0);
}

static ObjectGuid GetTransmogMenuGuid(Player* player, Creature* creature)
{
    return creature ? creature->GetGUID() : player->GetGUID();
}

static std::string GetRtgTransmogSlotStateMarker(Player* player, uint8 slot)
{
    if (!player)
        return {};

    Item* targetItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    uint32 targetEntry = targetItem ? targetItem->GetEntry() : 0u;
    uint32 fakeEntry = targetItem ? sT->GetFakeEntry(targetItem->GetGUID()) : 0u;
    uint32 visualEntry = fakeEntry && fakeEntry != HIDDEN_ITEM_ID ? fakeEntry : targetEntry;
    uint32 visualQuality = 0u;
    uint32 draftEntry = 0u;
    uint32 draftMode = 0u; // 0 none, 1 appearance, 2 hidden, 3 remove
    uint32 draftQuality = 0u;

    if (visualEntry)
        if (ItemTemplate const* visualTemplate = sObjectMgr->GetItemTemplate(visualEntry))
            visualQuality = visualTemplate->Quality;

    if (Transmogrification::outfitDraft const* draft = sT->GetOutfitDraft(player))
    {
        if (auto itr = draft->find(slot); itr != draft->end())
        {
            draftEntry = itr->second.appearanceEntry;
            if (draftEntry == HIDDEN_ITEM_ID)
                draftMode = 2;
            else if (draftEntry == 0)
                draftMode = 3;
            else
            {
                draftMode = 1;
                if (ItemTemplate const* previewTemplate = sObjectMgr->GetItemTemplate(draftEntry))
                    draftQuality = previewTemplate->Quality;
            }
        }
    }

    return " |cff010101[RTGTMOGSLOT:"
        + std::to_string(uint32(slot + 1)) + ":"
        + std::to_string(targetItem ? 1u : 0u) + ":"
        + std::to_string(targetEntry) + ":"
        + std::to_string(visualEntry) + ":"
        + std::to_string(visualQuality) + ":"
        + std::to_string(draftEntry) + ":"
        + std::to_string(draftMode) + ":"
        + std::to_string(draftQuality) + "]|r";
}

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_HOWWORKS = {
    {LOCALE_enUS, "How does transmogrification work?"},
    {LOCALE_koKR, "형상변환은 어떻게 작동합니까?"},
    {LOCALE_frFR, "Comment fonctionne la transmogrification ?"},
    {LOCALE_deDE, "Wie funktioniert Transmogrifizierung?"},
    {LOCALE_zhCN, "变形术是如何运作的？"},
    {LOCALE_zhTW, "幻化是如何運作的？"},
    {LOCALE_esES, "¿Cómo funciona la transfiguración?"},
    {LOCALE_esMX, "¿Cómo funciona la transfiguración?"},
    {LOCALE_ruRU, "Как работает трансмогрификация?"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_MANAGESETS = {
    {LOCALE_enUS, "Saved Outfits"},
    {LOCALE_koKR, "세트 관리"},
    {LOCALE_frFR, "Gérer les ensembles"},
    {LOCALE_deDE, "Sets verwalten"},
    {LOCALE_zhCN, "管理套装"},
    {LOCALE_zhTW, "管理套裝"},
    {LOCALE_esES, "Administrar conjuntos"},
    {LOCALE_esMX, "Administrar conjuntos"},
    {LOCALE_ruRU, "Управление комплектами"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_REMOVETRANSMOG = {
    {LOCALE_enUS, "Remove all transmogrifications"},
    {LOCALE_koKR, "모든 변형 제거"},
    {LOCALE_frFR, "Supprimer toutes les transmogrifications"},
    {LOCALE_deDE, "Alle Transmogrifikationen entfernen"},
    {LOCALE_zhCN, "移除所有幻化"},
    {LOCALE_zhTW, "移除所有幻化"},
    {LOCALE_esES, "Eliminar todas las transfiguraciones"},
    {LOCALE_esMX, "Eliminar todas las transfiguraciones"},
    {LOCALE_ruRU, "Удалить все трансмогрификации"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_REMOVETRANSMOG_ASK = {
    {LOCALE_enUS, "Remove transmogrifications from all equipped items?"},
    {LOCALE_koKR, "장착한 모든 아이템의 변형을 제거합니까?"},
    {LOCALE_frFR, "Supprimer les transmogrifications de tous les objets équipés ?"},
    {LOCALE_deDE, "Transmogrifikationen von allen ausgerüsteten Gegenständen entfernen?"},
    {LOCALE_zhCN, "是否要从所有已装备的物品中移除幻化？"},
    {LOCALE_zhTW, "從所有已裝備物品中移除幻化？"},
    {LOCALE_esES, "¿Eliminar las transfiguraciones de todos los objetos equipados?"},
    {LOCALE_esMX, "¿Eliminar las transfiguraciones de todos los objetos equipados?"},
    {LOCALE_ruRU, "Удалить трансмогрификации со всех экипированных предметов?"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_UPDATEMENU = {
    {LOCALE_enUS, "Update menu"},
    {LOCALE_koKR, "메뉴 업데이트"},
    {LOCALE_frFR, "Mettre à jour le menu"},
    {LOCALE_deDE, "Menü aktualisieren"},
    {LOCALE_zhCN, "更新菜单"},
    {LOCALE_zhTW, "更新選單"},
    {LOCALE_esES, "Actualizar menú"},
    {LOCALE_esMX, "Actualizar menú"},
    {LOCALE_ruRU, "Обновить меню"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_HOWSETSWORK = {
    {LOCALE_enUS, "How do Saved Outfits work?"},
    {LOCALE_koKR, "세트는 어떻게 작동합니까?"},
    {LOCALE_frFR, "Comment fonctionnent les ensembles ?"},
    {LOCALE_deDE, "Wie funktionieren Sets?"},
    {LOCALE_zhCN, "套装是如何运作的？"},
    {LOCALE_zhTW, "套裝如何運作？"},
    {LOCALE_esES, "¿Cómo funcionan los conjuntos?"},
    {LOCALE_esMX, "¿Cómo funcionan los conjuntos?"},
    {LOCALE_ruRU, "Как работают комплекты?"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_SAVESET = {
    {LOCALE_enUS, "Save Outfit"},
    {LOCALE_koKR, "세트 저장"},
    {LOCALE_frFR, "Sauvegarder l'ensemble"},
    {LOCALE_deDE, "Set speichern"},
    {LOCALE_zhCN, "保存套装"},
    {LOCALE_zhTW, "儲存套裝"},
    {LOCALE_esES, "Guardar conjunto"},
    {LOCALE_esMX, "Guardar conjunto"},
    {LOCALE_ruRU, "Сохранить комплект"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_BACK = {
    {LOCALE_enUS, "Back..."},
    {LOCALE_koKR, "뒤로..."},
    {LOCALE_frFR, "Retour..."},
    {LOCALE_deDE, "Zurück..."},
    {LOCALE_zhCN, "返回..."},
    {LOCALE_zhTW, "返回..."},
    {LOCALE_esES, "Atrás..."},
    {LOCALE_esMX, "Atrás..."},
    {LOCALE_ruRU, "Назад..."}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_BACK_TO_SCOREBOARD = {
    {LOCALE_enUS, "Return to Cosmetics"},
    {LOCALE_koKR, "Return to Cosmetics"},
    {LOCALE_frFR, "Return to Cosmetics"},
    {LOCALE_deDE, "Return to Cosmetics"},
    {LOCALE_zhCN, "Return to Cosmetics"},
    {LOCALE_zhTW, "Return to Cosmetics"},
    {LOCALE_esES, "Return to Cosmetics"},
    {LOCALE_esMX, "Return to Cosmetics"},
    {LOCALE_ruRU, "Return to Cosmetics"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_USESET = {
    {LOCALE_enUS, "Preview this outfit"},
    {LOCALE_koKR, "이 세트를 사용"},
    {LOCALE_frFR, "Utiliser cet ensemble"},
    {LOCALE_deDE, "Dieses Set verwenden"},
    {LOCALE_zhCN, "使用此套装"},
    {LOCALE_zhTW, "使用此套裝"},
    {LOCALE_esES, "Usar este conjunto"},
    {LOCALE_esMX, "Usar este conjunto"},
    {LOCALE_ruRU, "Использовать этот комплект"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_CONFIRM_USESET = {
    {LOCALE_enUS, "Preview this Saved Outfit and review every available slot before applying it. Nothing is charged until final confirmation.\nContinue?\n\n"},
    {LOCALE_koKR, "이 세트를 변형에 사용하면 변형된 아이템이 계정에 제한되어 환불 및 거래가 불가능합니다.\n계속하시겠습니까?\n\n"},
    {LOCALE_frFR, "En utilisant cet ensemble pour la transmogrification, les objets transmogrifiés seront liés à votre personnage et deviendront non remboursables et non échangeables.\nVoulez-vous continuer ?\n\n"},
    {LOCALE_deDE, "Wenn du dieses Set für die Transmogrifikation verwendest, werden die transmogrifizierten Gegenstände an dich gebunden und können nicht erstattet oder gehandelt werden.\nMöchtest du fortfahren?\n\n"},
    {LOCALE_zhCN, "将此套装用于幻化将使幻化后的物品与您绑定，并使其不可退还和不可交易。\n您是否要继续？\n\n"},
    {LOCALE_zhTW, "使用此套裝進行幻化將使幻化後的物品與您綁定，並使其無法退款和無法交易。\n您是否希望繼續？\n\n"},
    {LOCALE_esES, "Usar este conjunto para transfigurar vinculará los objetos transfigurados a ti y los volverá no reembolsables y no intercambiables.\n¿Deseas continuar?\n\n"},
    {LOCALE_esMX, "Usar este conjunto para transfigurar vinculará los objetos transfigurados a ti y los volverá no reembolsables y no intercambiables.\n¿Deseas continuar?\n\n"},
    {LOCALE_ruRU, "Использование этого комплекта для трансмогрификации привяжет трансмогрифицированные предметы к вам и сделает их неподлежащими возврату и обмену.\nЖелаете продолжить?\n\n"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_DELETESET = {
    {LOCALE_enUS, "Delete Outfit"},
    {LOCALE_koKR, "세트 삭제"},
    {LOCALE_frFR, "Supprimer l'ensemble"},
    {LOCALE_deDE, "Set löschen"},
    {LOCALE_zhCN, "删除套装"},
    {LOCALE_zhTW, "刪除套裝"},
    {LOCALE_esES, "Eliminar conjunto"},
    {LOCALE_esMX, "Eliminar conjunto"},
    {LOCALE_ruRU, "Удалить комплект"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_CONFIRM_DELETESET = {
    {LOCALE_enUS, "Are you sure you want to delete the Saved Outfit "},
    {LOCALE_koKR, "을(를) 삭제하시겠습니까 "},
    {LOCALE_frFR, "Êtes-vous sûr de vouloir supprimer "},
    {LOCALE_deDE, "Möchten Sie wirklich löschen "},
    {LOCALE_zhCN, "您确定要删除吗 "},
    {LOCALE_zhTW, "您確定要刪除 "},
    {LOCALE_esES, "¿Estás seguro de que quieres eliminar "},
    {LOCALE_esMX, "¿Estás seguro de que quieres eliminar "},
    {LOCALE_ruRU, "Вы уверены, что хотите удалить "}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_INSERTSETNAME = {
    {LOCALE_enUS, "Enter outfit name"},
    {LOCALE_koKR, "세트 이름 입력"},
    {LOCALE_frFR, "Insérer le nom de l'ensemble"},
    {LOCALE_deDE, "Set-Namen einfügen"},
    {LOCALE_zhCN, "插入套装名称"},
    {LOCALE_zhTW, "輸入套裝名稱"},
    {LOCALE_esES, "Insertar nombre del conjunto"},
    {LOCALE_esMX, "Insertar nombre del conjunto"},
    {LOCALE_ruRU, "Введите имя комплекта"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_SEARCH = {
    {LOCALE_enUS, "Search..."},
    {LOCALE_koKR, "검색..."},
    {LOCALE_frFR, "Rechercher..."},
    {LOCALE_deDE, "Suche..."},
    {LOCALE_zhCN, "搜索..."},
    {LOCALE_zhTW, "搜索..."},
    {LOCALE_esES, "Buscar..."},
    {LOCALE_esMX, "Buscar..."},
    {LOCALE_ruRU, "Поиск..."}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_SEARCHING_FOR = {
    {LOCALE_enUS, "Searching for: "},
    {LOCALE_koKR, "검색 중: "},
    {LOCALE_frFR, "Recherche en cours: "},
    {LOCALE_deDE, "Suche nach: "},
    {LOCALE_zhCN, "正在搜索： "},
    {LOCALE_zhTW, "正在搜尋："},
    {LOCALE_esES, "Buscando:" },
    {LOCALE_esMX, "Buscando: "},
    {LOCALE_ruRU, "Поиск: "}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_SEARCH_FOR_ITEM = {
    {LOCALE_enUS, "Search for what item?"},
    {LOCALE_koKR, "어떤 아이템을 찾으시겠습니까?"},
    {LOCALE_frFR, "Rechercher quel objet ?"},
    {LOCALE_deDE, "Nach welchem Gegenstand suchen?"},
    {LOCALE_zhCN, "搜索哪个物品？"},
    {LOCALE_zhTW, "搜索哪個物品？"},
    {LOCALE_esES, "¿Buscar un objeto?"},
    {LOCALE_esMX, "¿Buscar un objeto?"},
    {LOCALE_ruRU, "Поиск предмета:"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_CONFIRM_HIDE_ITEM = {
    {LOCALE_enUS, "You are hiding the item in this slot.\nDo you wish to continue?\n\n"},
    {LOCALE_koKR, "이 슬롯에 아이템을 감추고 있습니다.\n계속하시겠습니까?\n\n"},
    {LOCALE_frFR, "Vous masquez l'objet dans cet emplacement.\nVoulez-vous continuer ?\n\n"},
    {LOCALE_deDE, "Du versteckst das Item in diesem Slot.\nMöchtest du fortfahren?\n\n"},
    {LOCALE_zhCN, "您正在隐藏此槽中的物品。\n您是否要继续？\n\n"},
    {LOCALE_zhTW, "您正在隱藏此槽中的物品。\n您是否希望繼續？\n\n"},
    {LOCALE_esES, "Estás ocultando el objeto en esta ranura.\n¿Deseas continuar?\n\n"},
    {LOCALE_esMX, "Estás ocultando el objeto en esta ranura.\n¿Deseas continuar?\n\n"},
    {LOCALE_ruRU, "Вы скрываете предмет в этом слоте.\nЖелаете продолжить?\n\n"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_HIDESLOT = {
    {LOCALE_enUS, "Hide Slot"},
    {LOCALE_koKR, "슬롯 숨기기"},
    {LOCALE_frFR, "Cacher l'emplacement"},
    {LOCALE_deDE, "Slot verbergen"},
    {LOCALE_zhCN, "隐藏槽位"},
    {LOCALE_zhTW, "隱藏槽位"},
    {LOCALE_esES, "Ocultar ranura"},
    {LOCALE_esMX, "Ocultar ranura"},
    {LOCALE_ruRU, "Скрыть слот"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_REMOVETRANSMOG_SLOT = {
    {LOCALE_enUS, "Remove transmogrification from the slot?"},
    {LOCALE_koKR, "해당 슬롯의 형상변환을 제거합니까?"},
    {LOCALE_frFR, "Supprimer la transmogrification de l'emplacement ?"},
    {LOCALE_deDE, "Transmogrifikation aus dem Slot entfernen?"},
    {LOCALE_zhCN, "是否要从该槽位中移除幻化？"},
    {LOCALE_zhTW, "從該槽位移除幻化？"},
    {LOCALE_esES, "¿Eliminar la transfiguración del espacio?"},
    {LOCALE_esMX, "¿Eliminar la transfiguración del espacio?"},
    {LOCALE_ruRU, "Удалить трансмогрификацию из ячейки?"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_CONFIRM_USEITEM = {
    {LOCALE_enUS, "Using this item for transmogrify will bind it to you and make it non-refundable and non-tradeable.\nDo you wish to continue?\n\n"},
    {LOCALE_koKR, "이 아이템을 변형에 사용하면 계정에 제한되어 환불 및 거래가 불가능하게 됩니다.\n계속하시겠습니까?\n\n"},
    {LOCALE_frFR, "En utilisant cet objet pour la transmogrification, il sera lié à votre personnage et deviendra non remboursable et non échangeable.\nVoulez-vous continuer ?\n\n"},
    {LOCALE_deDE, "Wenn du diesen Gegenstand für die Transmogrifikation verwendest, wird er an dich gebunden und kann nicht erstattet oder gehandelt werden.\nMöchtest du fortfahren?\n\n"},
    {LOCALE_zhCN, "将此物品用于幻化将使其与您绑定，并使其不可退还和不可交易。\n您是否要继续？\n\n"},
    {LOCALE_zhTW, "使用此物品進行幻化將使其與您綁定，並使其無法退款和無法交易。\n您是否希望繼續？\n\n"},
    {LOCALE_esES, "Usar este objeto para transfigurar lo vinculará a ti y lo volverá no reembolsable y no intercambiable.\n¿Deseas continuar?\n\n"},
    {LOCALE_esMX, "Usar este objeto para transfigurar lo vinculará a ti y lo volverá no reembolsable y no intercambiable.\n¿Deseas continuar?\n\n"},
    {LOCALE_ruRU, "Использование этого предмета для трансмогрификации привяжет его к вам и сделает его неподлежащим возврату и обмену.\nЖелаете продолжить?\n\n"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_PREVIOUS_PAGE = {
    {LOCALE_enUS, "Previous Page"},
    {LOCALE_koKR, "이전 페이지"},
    {LOCALE_frFR, "Page précédente"},
    {LOCALE_deDE, "Vorherige Seite"},
    {LOCALE_zhCN, "上一页"},
    {LOCALE_zhTW, "上一頁"},
    {LOCALE_esES, "Página anterior"},
    {LOCALE_esMX, "Página anterior"},
    {LOCALE_ruRU, "Предыдущая страница"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_NEXT_PAGE = {
    {LOCALE_enUS, "Next Page"},
    {LOCALE_koKR, "다음 페이지"},
    {LOCALE_frFR, "Page suivante"},
    {LOCALE_deDE, "Nächste Seite"},
    {LOCALE_zhCN, "下一页"},
    {LOCALE_zhTW, "下一頁"},
    {LOCALE_esES, "Página siguiente"},
    {LOCALE_esMX, "Página siguiente"},
    {LOCALE_ruRU, "Следующая страница"}
};

const std::unordered_map<LocaleConstant, std::string> TRANSMOG_TEXT_ADDED_APPEARANCE = {
    {LOCALE_enUS, "has been added to your appearance collection."},
    {LOCALE_koKR, "이(가) 외형 컬렉션에 추가되었습니다."},
    {LOCALE_frFR, "a été ajouté(e) à votre collection d'apparences."},
    {LOCALE_deDE, "wurde deiner Transmog-Sammlung hinzugefügt."},
    {LOCALE_zhCN, "已添加到外观收藏中。"},
    {LOCALE_zhTW, "已加入您的外觀收藏。"},
    {LOCALE_esES, "se ha añadido a tu colección de apariencias."},
    {LOCALE_esMX, "se ha agregado a tu colección de apariencias."},
    {LOCALE_ruRU, "был добавлен в вашу коллекцию обликов."}
};

std::unordered_map<std::string, const std::unordered_map<LocaleConstant, std::string>*> textMaps = {
    {"how_works", &TRANSMOG_TEXT_HOWWORKS},
    {"manage_sets", &TRANSMOG_TEXT_MANAGESETS},
    {"remove_transmog", &TRANSMOG_TEXT_REMOVETRANSMOG},
    {"remove_transmog_ask", &TRANSMOG_TEXT_REMOVETRANSMOG_ASK},
    {"update_menu", &TRANSMOG_TEXT_UPDATEMENU},
    {"how_sets_work", &TRANSMOG_TEXT_HOWSETSWORK},
    {"save_set", &TRANSMOG_TEXT_SAVESET},
    {"back", &TRANSMOG_TEXT_BACK},
    {"back_to_scoreboard", &TRANSMOG_TEXT_BACK_TO_SCOREBOARD},
    {"use_set", &TRANSMOG_TEXT_USESET},
    {"confirm_use_set", &TRANSMOG_TEXT_CONFIRM_USESET},
    {"delete_set", &TRANSMOG_TEXT_DELETESET},
    {"confirm_delete_set", &TRANSMOG_TEXT_CONFIRM_DELETESET},
    {"insert_set_name", &TRANSMOG_TEXT_INSERTSETNAME},
    {"search", &TRANSMOG_TEXT_SEARCH},
    {"searching_for", &TRANSMOG_TEXT_SEARCHING_FOR},
    {"search_for_item", &TRANSMOG_TEXT_SEARCH_FOR_ITEM},
    {"confirm_hide_item", &TRANSMOG_TEXT_CONFIRM_HIDE_ITEM},
    {"hide_slot", &TRANSMOG_TEXT_HIDESLOT},
    {"remove_transmog_slot", &TRANSMOG_TEXT_REMOVETRANSMOG_SLOT},
    {"confirm_use_item", &TRANSMOG_TEXT_CONFIRM_USEITEM},
    {"previous_page", &TRANSMOG_TEXT_PREVIOUS_PAGE},
    {"next_page", &TRANSMOG_TEXT_NEXT_PAGE},
    {"added_appearance", &TRANSMOG_TEXT_ADDED_APPEARANCE}
};

const uint32 FALLBACK_HIDE_ITEM_VENDOR_ID   = 9172; //Invisibility potion
const uint32 FALLBACK_REMOVE_TMOG_VENDOR_ID = 1049; //Tablet of Purge
const uint32 CUSTOM_HIDE_ITEM_VENDOR_ID     = 57575;//Custom Hide Item item
const uint32 CUSTOM_REMOVE_TMOG_VENDOR_ID   = 57576;//Custom Remove Transmog item

std::string GetLocaleText(LocaleConstant locale, const std::string& titleType) {
    auto textMapIt = textMaps.find(titleType);
    if (textMapIt != textMaps.end()) {
        const std::unordered_map<LocaleConstant, std::string>* textMap = textMapIt->second;
        auto it = textMap->find(locale);
        if (it != textMap->end()) {
            return it->second;
        }
    }

    return "";
}

uint32 GetTransmogPrice(ItemTemplate const* targetItem)
{
    if (!targetItem)
        return 0;

    double const scaled = double(sT->GetSpecialPrice(targetItem)) * double(sT->GetScaledCostModifier());
    if (!std::isfinite(scaled))
        return uint32(std::numeric_limits<int32>::max());

    double const total = scaled + double(sT->GetCopperCost());
    if (total <= 0.0)
        return 0;
    if (total >= double(std::numeric_limits<int32>::max()))
        return uint32(std::numeric_limits<int32>::max());
    return uint32(total);
}

uint32 GetTransmogVotePointPrice(uint32 copperPrice)
{
    if (!copperPrice)
        return 0;

    uint64 const maximumCharge = uint64(std::numeric_limits<int32>::max());
    uint64 const flat = std::min<uint64>(sT->GetVotePointsFlatCost(), maximumCharge);
    float const perGold = sT->GetVotePointsPerGold();
    if (!std::isfinite(perGold) || perGold <= 0.0f)
        return uint32(flat);

    double const scaled = std::ceil((double(copperPrice) / 10000.0) * double(perGold));
    if (!std::isfinite(scaled) || scaled >= double(maximumCharge - flat))
        return uint32(maximumCharge);
    return uint32(flat + uint64(std::max(0.0, scaled)));
}

#ifdef PRESETS
int32 GetTransmogSetPrice(uint64 baseCopper)
{
    double const scaled = double(baseCopper) * double(sT->GetSetCostModifier());
    if (!std::isfinite(scaled))
        return std::numeric_limits<int32>::max();

    double const total = scaled + double(sT->GetSetCopperCost());
    if (total <= 0.0)
        return 0;
    if (total >= double(std::numeric_limits<int32>::max()))
        return std::numeric_limits<int32>::max();
    return int32(total);
}
#endif

bool ValidForTransmog (Player* player, ItemTemplate const* targetTemplate, ItemTemplate const* sourceTemplate, bool hasSearch, std::string const& searchTerm)
{
    if (!player || !targetTemplate || !sourceTemplate)
        return false;

    if (!sT->CanTransmogrifyItemWithItem(player, targetTemplate, sourceTemplate))
        return false;

    // Keep the currently-applied appearance in the browser. This makes it clear that
    // an already-transmogrified slot can still be opened and replaced directly.
    // PerformTransmogrification() treats selecting the current appearance as a no-op,
    // so the player is never charged for simply clicking it again.
    if (hasSearch && sourceTemplate->Name1.find(searchTerm) == std::string::npos)
        return false;

    return true;
}

bool CmpTmog (ItemTemplate const* first, ItemTemplate const* second)
{
    if (!first || !second)
        return second != nullptr;

    int const firstQuality = 7 - first->Quality;
    int const secondQuality = 7 - second->Quality;
    return std::tie(firstQuality, first->Name1, first->ItemId)
        < std::tie(secondQuality, second->Name1, second->ItemId);
}

std::vector<ItemTemplate const*> GetValidTransmogs (Player* player, Item* target, bool hasSearch, std::string const& searchTerm)
{
    std::vector<ItemTemplate const*> allowedItems;
    if (!player || !target)
        return allowedItems;

    ItemTemplate const* targetTemplate = target->GetTemplate();
    if (!targetTemplate)
        return allowedItems;

    // Collection mode adds permanently unlocked appearances, but a currently
    // carried item must also be usable immediately. Merge both sources and
    // deduplicate by entry. Work directly with immutable item templates: the
    // previous collection path allocated ownerless Item objects that were never
    // destroyed merely to access their templates.
    std::unordered_set<uint32> addedEntries;
    auto addValidSource = [&](ItemTemplate const* sourceTemplate)
    {
        if (!sourceTemplate)
            return;

        uint32 const entry = sourceTemplate->ItemId;
        if (addedEntries.find(entry) != addedEntries.end())
            return;

        if (!ValidForTransmog(player, targetTemplate, sourceTemplate, hasSearch, searchTerm))
            return;

        addedEntries.insert(entry);
        allowedItems.push_back(sourceTemplate);
    };

    if (sT->GetUseCollectionSystem())
    {
        uint32 const ownerGuid = player->GetGUID().GetCounter();
        auto const collectionItr = sT->collectionCache.find(ownerGuid);
        if (collectionItr != sT->collectionCache.end())
            for (uint32 itemId : collectionItr->second)
                addValidSource(sObjectMgr->GetItemTemplate(itemId));
    }

    // Always merge the player's live backpack and equipped bags. This preserves
    // immediate use of newly obtained BoE cloaks and other sources even before
    // they have been written to the collection cache.
    for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        if (Item* sourceItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            addValidSource(sourceItem->GetTemplate());

    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        Bag* bag = player->GetBagByPos(i);
        if (!bag)
            continue;

        for (uint32 j = 0; j < bag->GetBagSize(); ++j)
            if (Item* sourceItem = player->GetItemByPos(i, j))
                addValidSource(sourceItem->GetTemplate());
    }

    if (sConfigMgr->GetOption<bool>("Transmogrification.EnableSortByQualityAndName", true))
        std::sort(allowedItems.begin(), allowedItems.end(), CmpTmog);

    // The Trophy Collection preserves every earned item, while the appearance
    // picker presents one representative for each visual model. This keeps
    // character history complete without flooding dropdowns with identical art.
    std::unordered_set<uint32> addedDisplays;
    allowedItems.erase(std::remove_if(allowedItems.begin(), allowedItems.end(), [&](ItemTemplate const* item)
    {
        if (!item) return true;
        return !addedDisplays.insert(item->DisplayInfoID).second;
    }), allowedItems.end());

    return allowedItems;
}

static std::map<uint8, uint32> BuildCompleteOutfitLook(Player* player)
{
    std::map<uint8, uint32> items;
    if (!player || !player->GetSession())
        return items;

    // A saved outfit describes the character's complete visible transmog plan,
    // not only the slots that happened to be changed in the current preview.
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (!sT->GetSlotName(slot, player->GetSession()))
            continue;

        if (Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            items[slot] = sT->GetFakeEntry(equipped->GetGUID()); // 0 intentionally means original appearance.
    }

    if (Transmogrification::outfitDraft const* draft = sT->GetOutfitDraft(player))
        for (auto const& [slot, staged] : *draft)
            items[slot] = staged.appearanceEntry;

    return items;
}

static std::string FormatSavedOutfitAppearance(Player* player, uint8 slot, uint32 entry)
{
    if (!player || !player->GetSession())
        return {};

    char const* rawSlotName = sT->GetSlotName(slot, player->GetSession());
    std::string slotName = rawSlotName ? rawSlotName : "Equipment Slot";
    std::string appearance;
    if (entry == 0)
        appearance = "Original appearance";
    else if (entry == HIDDEN_ITEM_ID)
        appearance = "Hidden slot";
    else
        appearance = sT->GetItemIcon(entry, 30, 30, -18, 0) + sT->GetItemLink(entry, player->GetSession());

    return "|cffffcc00" + slotName + ":|r " + appearance;
}

static std::string SerializeSavedOutfit(std::map<uint8, uint32> const& items)
{
    std::ostringstream data;
    for (auto const& [slot, entry] : items)
        data << uint32(slot) << ' ' << entry << ' ';
    return data.str();
}

static bool SavedOutfitEntryIsAvailable(Player* player, uint8 slot, uint32 entry)
{
    if (!player || slot >= EQUIPMENT_SLOT_END)
        return false;
    Item* target = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!target)
        return false;
    if (entry == 0)
        return true;
    if (entry == HIDDEN_ITEM_ID)
        return sT->GetAllowHiddenTransmog();
    ItemTemplate const* source = sObjectMgr->GetItemTemplate(entry);
    return source && sT->CharacterCanUseAppearance(player, entry)
        && sT->CanTransmogrifyItemWithItem(player, target->GetTemplate(), source);
}

static std::vector<uint8> FindUnavailableSavedOutfitSlots(Player* player, std::map<uint8, uint32> const& items)
{
    std::vector<uint8> unavailable;
    for (auto const& [slot, entry] : items)
        if (!SavedOutfitEntryIsAvailable(player, slot, entry))
            unavailable.push_back(slot);
    return unavailable;
}

static std::string FormatOutfitCopper(uint64 copper)
{
    uint64 gold = copper / 10000ULL;
    uint64 silver = (copper % 10000ULL) / 100ULL;
    uint64 coins = copper % 100ULL;
    std::ostringstream text;
    if (gold) text << gold << "g ";
    if (silver || gold) text << silver << "s ";
    text << coins << "c";
    return text.str();
}

static std::string GetRtgOutfitSummaryMarker(Player* player)
{
    if (!player)
        return {};
    Transmogrification::outfitDraft const* draft = sT->GetOutfitDraft(player);
    if (!draft)
        return {};

    std::string error;
    Transmogrification::OutfitCostSummary cost = sT->CalculateOutfitCost(player, &error);
    return " |cff010101[RTGTMOGOUTFIT:"
        + std::to_string(draft->size()) + ":"
        + std::to_string(cost.changedSlots) + ":"
        + std::to_string(cost.copper) + ":"
        + std::to_string(cost.votePoints) + ":"
        + std::to_string(cost.tokens) + ":"
        + std::to_string(cost.freeOutfit ? 1u : 0u) + ":"
        + std::to_string(error.empty() ? 1u : 0u) + "]|r";
}

static void ShowOutfitReview(Player* player, Creature* creature)
{
    if (!player || !player->GetSession())
        return;
    WorldSession* session = player->GetSession();
    Transmogrification::outfitDraft const* draft = sT->GetOutfitDraft(player);
    if (!draft)
    {
        ChatHandler(session).SendSysMessage("Choose one or more appearances first. Nothing has been purchased yet.");
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
            "|TInterface/ICONS/Ability_Spy:30:30:-18:0|tBack to Transmog Studio",
            EQUIPMENT_SLOT_END + 1, 0);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, GetTransmogMenuGuid(player, creature));
        return;
    }

    std::string error;
    Transmogrification::OutfitCostSummary cost = sT->CalculateOutfitCost(player, &error);
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
        "|TInterface/ICONS/INV_Misc_Statue_02:30:30:-18:0|t|cffffcc00Outfit Preview|r — "
            + std::to_string(draft->size()) + " staged slot" + (draft->size() == 1 ? "" : "s")
            + GetRtgOutfitSummaryMarker(player),
        TRANSMOG_OUTFIT_REVIEW_SENDER, 0);

    for (auto const& [slot, staged] : *draft)
    {
        std::string slotName = sT->GetSlotName(slot, session) ? sT->GetSlotName(slot, session) : "Equipment Slot";
        std::string appearance;
        if (staged.appearanceEntry == 0)
            appearance = "Restore original appearance";
        else if (staged.appearanceEntry == HIDDEN_ITEM_ID)
            appearance = "Hide this slot";
        else
            appearance = sT->GetItemIcon(staged.appearanceEntry, 26, 26, -16, 0) + sT->GetItemLink(staged.appearanceEntry, session);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
            "|cffffcc00" + slotName + ":|r " + appearance,
            TRANSMOG_OUTFIT_REVIEW_SENDER, 0);
    }

    std::string priceText;
    if (!error.empty())
        priceText = "|cffff5555" + error + "|r";
    else if (cost.freeOutfit)
        priceText = "|cff00ff00Your ready free use covers this complete outfit.|r";
    else
    {
        std::vector<std::string> parts;
        if (cost.copper) parts.push_back(FormatOutfitCopper(cost.copper));
        if (cost.votePoints) parts.push_back(std::to_string(cost.votePoints) + " VP");
        if (cost.tokens) parts.push_back(std::to_string(cost.tokens) + " transmog token" + (cost.tokens == 1 ? "" : "s"));
        if (parts.empty()) priceText = "No charge";
        else
        {
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                if (i) priceText += " + ";
                priceText += parts[i];
            }
        }
    }
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
        "|TInterface/ICONS/INV_Misc_Coin_01:30:30:-18:0|tCombined cost: " + priceText,
        TRANSMOG_OUTFIT_REVIEW_SENDER, 0);

    if (error.empty() && cost.changedSlots)
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
            "|TInterface/Buttons/UI-CheckBox-Check:30:30:-18:0|t|cff00ff00Apply Complete Outfit|r",
            TRANSMOG_OUTFIT_APPLY_SENDER, 0,
            "Apply every reviewed appearance as one complete outfit?\n\n" + priceText,
            0, false);
#ifdef PRESETS
    if (sT->GetEnableSets() && sT->presetByName[player->GetGUID()].size() < sT->GetMaxSets())
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
            "|TInterface/GuildBankFrame/UI-GuildBankFrame-NewTab:30:30:-18:0|tSave Preview as Outfit",
            EncodeTransmogCodeSender(0), 0,
            "Name this character-specific outfit. Saving is free and does not apply or purchase it.",
            0, true);
#endif
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
        "|TInterface/PaperDollInfoFrame/UI-GearManager-LeaveItem-Opaque:30:30:-18:0|tClear Preview",
        TRANSMOG_OUTFIT_CLEAR_SENDER, 0);
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
        "|TInterface/ICONS/Ability_Spy:30:30:-18:0|tBack to Transmog Studio",
        EQUIPMENT_SLOT_END + 1, 0);
    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, GetTransmogMenuGuid(player, creature));
}

static void ShowTrophyCollection(Player* player, Creature* creature, uint32 page)
{
    if (!player || !player->GetSession()) return;
    WorldSession* session = player->GetSession();
    uint32 ownerGuid = player->GetGUID().GetCounter();
    std::vector<uint32> trophies;
    if (auto itr = sT->collectionCache.find(ownerGuid); itr != sT->collectionCache.end())
        trophies.assign(itr->second.begin(), itr->second.end());
    std::sort(trophies.begin(), trophies.end(), [](uint32 left, uint32 right)
    {
        ItemTemplate const* a=sObjectMgr->GetItemTemplate(left); ItemTemplate const* b=sObjectMgr->GetItemTemplate(right);
        if (!a || !b) return b != nullptr;
        if (a->Quality != b->Quality) return a->Quality > b->Quality;
        if (a->Name1 != b->Name1) return a->Name1 < b->Name1;
        return left < right;
    });

    std::unordered_set<uint32> displays;
    for (uint32 entry : trophies)
        if (ItemTemplate const* item=sObjectMgr->GetItemTemplate(entry)) displays.insert(item->DisplayInfoID);
    uint32 pages = std::max<uint32>(1, uint32((trophies.size()+TRANSMOG_TROPHY_PAGE_SIZE-1)/TRANSMOG_TROPHY_PAGE_SIZE));
    page = std::min<uint32>(std::max<uint32>(1,page),pages);
    AddGossipItemFor(player,GOSSIP_ICON_MONEY_BAG,
        "|TInterface/ICONS/INV_Misc_Statue_02:30:30:-18:0|t|cffffcc00Character Trophy Collection|r — "
        + std::to_string(trophies.size()) + " items, " + std::to_string(displays.size()) + " unique appearances",
        TRANSMOG_TROPHY_SENDER,page);

    uint32 begin=(page-1)*TRANSMOG_TROPHY_PAGE_SIZE;
    uint32 finish=std::min<uint32>(uint32(trophies.size()),begin+TRANSMOG_TROPHY_PAGE_SIZE);
    struct TrophyMeta { uint32 discoveredAt=0; std::string source="legacy"; bool legacy=true; };
    std::unordered_map<uint32,TrophyMeta> metadata;
    if (begin < finish)
    {
        std::ostringstream ids;
        for (uint32 i=begin;i<finish;++i) { if (i>begin) ids << ','; ids << trophies[i]; }
        if (QueryResult result=CharacterDatabase.Query(
            "SELECT `item_template_id`,`first_discovered_at`,`discovery_source`,`legacy_discovery` "
            "FROM `custom_unlocked_appearances` WHERE `owner_guid`={} AND `item_template_id` IN ({})",
            ownerGuid,ids.str()))
        do { Field* f=result->Fetch(); metadata[f[0].Get<uint32>()]={f[1].Get<uint32>(),f[2].Get<std::string>(),f[3].Get<uint8>()!=0}; } while(result->NextRow());
    }
    auto sourceLabel=[](std::string source)
    {
        if (source=="loot") return std::string("Looted");
        if (source=="crafted") return std::string("Crafted");
        if (source=="equip") return std::string("Equipped");
        if (source=="purchase") return std::string("Purchased");
        if (source=="acquired") return std::string("Acquired");
        if (source=="retroactive-owned") return std::string("Owned at wardrobe launch");
        if (source=="retroactive-bank") return std::string("Stored in bank at wardrobe launch");
        return std::string("Legacy discovery");
    };
    for (uint32 i=begin;i<finish;++i)
    {
        uint32 entry=trophies[i];
        ItemTemplate const* item=sObjectMgr->GetItemTemplate(entry);
        if (!item) continue;
        TrophyMeta meta; if (auto itr=metadata.find(entry);itr!=metadata.end()) meta=itr->second;
        std::string detail=sourceLabel(meta.source);
        if (std::string date = FormatTrophyDate(meta.discoveredAt); !date.empty()) detail += " • " + date;
        AddGossipItemFor(player,GOSSIP_ICON_MONEY_BAG,
            sT->GetItemIcon(entry,28,28,-17,0)+sT->GetItemLink(entry,session)
            + " |cff8f8068— " + detail + " • Find Source in RTGHead|r",
            TRANSMOG_TROPHY_SOURCE_SENDER,entry);
    }
    if (page>1) AddGossipItemFor(player,GOSSIP_ICON_CHAT,"Previous Page",TRANSMOG_TROPHY_SENDER,page-1);
    if (page<pages) AddGossipItemFor(player,GOSSIP_ICON_CHAT,"Next Page",TRANSMOG_TROPHY_SENDER,page+1);
    AddGossipItemFor(player,GOSSIP_ICON_MONEY_BAG,
        "|TInterface/ICONS/Ability_Spy:30:30:-18:0|tBack to Transmog Studio",
        EQUIPMENT_SLOT_END+1,0);
    SendGossipMenuFor(player,DEFAULT_GOSSIP_MESSAGE,GetTransmogMenuGuid(player,creature));
}

bool PerformTransmogrification(Player* player, uint32 itemEntry, uint32 /*cost*/)
{
    uint8 slot = sT->selectionCache[player->GetGUID()];
    WorldSession* session = player->GetSession();

    Item* targetItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!targetItem)
    {
        ChatHandler(session).SendNotification(LANG_ERR_TRANSMOG_MISSING_DEST_ITEM);
        return false;
    }

    uint32 existingTransmog = sT->GetFakeEntry(targetItem->GetGUID());
    if (existingTransmog == itemEntry)
    {
        session->SendAreaTriggerMessage("That appearance is already applied. Choose another appearance to replace it.");
        return true;
    }

    bool freeReadyBefore = sT->HasFreeTransmogReady(player);

    // IMPORTANT:
    // Do NOT pre-check gold here.
    // Payment may be Vote Points (or mixed), and Transmogrify() already performs the correct
    // currency validation and charges the appropriate currencies.
    TransmogAcoreStrings res = sT->Transmogrify(player, itemEntry, slot);

    if (res == LANG_ERR_TRANSMOG_OK)
    {
        if (freeReadyBefore && !sT->HasFreeTransmogReady(player))
        {
            session->SendAreaTriggerMessage("Free transmog used! Next free use in {}.",
                sT->FormatFreeTransmogCooldown(sT->GetFreeTransmogCooldownSeconds()));
        }
        else
            session->SendAreaTriggerMessage("{}", GTS(LANG_ERR_TRANSMOG_OK));

        return true;
    }

    ChatHandler(session).SendNotification(res);
    return false;
}

void RemoveTransmogrification (Player* player)
{
    uint8 slot = sT->selectionCache[player->GetGUID()];
    WorldSession* session = player->GetSession();
    if (Item* newItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
    {
        if (sT->GetFakeEntry(newItem->GetGUID()))
        {
            sT->DeleteFakeEntry(player, slot, newItem);
            session->SendAreaTriggerMessage("{}", GTS(LANG_ERR_UNTRANSMOG_OK));
        }
        else
            ChatHandler(session).SendNotification(LANG_ERR_UNTRANSMOG_NO_TRANSMOGS);
    }
}

class npc_transmogrifier : public CreatureScript
{
public:
    npc_transmogrifier() : CreatureScript("npc_transmogrifier") { }

    struct npc_transmogrifierAI : ScriptedAI
    {
        npc_transmogrifierAI(Creature* creature) : ScriptedAI(creature) { };

        bool CanBeSeen(Player const* player) override
        {
            Player* target = ObjectAccessor::FindConnectedPlayer(player->GetGUID());

            if (TempSummon* summon = me->ToTempSummon())
            {
                return summon->GetOwner() == player;
            }

            return sTransmogrification->IsEnabled() && (target && !target->GetPlayerSetting("mod-transmog", SETTING_HIDE_TRANSMOG).IsEnabled());
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_transmogrifierAI(creature);
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!player || !player->GetSession())
            return false;

        WorldSession* session = player->GetSession();
        if (!sT->IsEnabled() || (!creature && !sT->IsPortableNPCEnabled))
        {
            ChatHandler(session).SendSysMessage("Portable Transmog is currently unavailable.");
            CloseGossipMenuFor(player);
            return true;
        }

        LocaleConstant locale = session->GetSessionDbLocaleIndex();

        // Clear the search string for the player
        sT->searchStringByPlayer.erase(player->GetGUID().GetCounter());

        // Scoreboard-opened transmog has no creature context, so give players a direct return path.
        if (!creature)
            AddGossipItemFor(player, GOSSIP_ICON_TALK, "|TInterface/PaperDollInfoFrame/UI-GearManager-Undo:30:30:-18:0|t |cff3b2a1a" + GetLocaleText(locale, "back_to_scoreboard") + "|r", TRANSMOG_SCOREBOARD_RETURN_SENDER, 0);

        if (sT->GetFreeTransmogEnabled())
        {
            uint32 freeRemaining = sT->GetFreeTransmogCooldownRemaining(player);
            std::string freeText = "|TInterface/ICONS/INV_Misc_Gift_01:30:30:-18:0|t";
            if (freeRemaining == 0)
                freeText += "|cff00ff00Free Transmog Ready|r";
            else
                freeText += "|cffffcc00Free Transmog: " + sT->FormatFreeTransmogCooldown(freeRemaining) + " cooldown|r";
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, freeText, EQUIPMENT_SLOT_END + 1, 0);
        }

        if (sT->GetEnableTransmogInfo())
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/INV_Misc_Book_11:30:30:-18:0|t" + GetLocaleText(locale, "how_works"), EQUIPMENT_SLOT_END + 9, 0);
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (const char* slotName = sT->GetSlotName(slot, session))
            {
                Item* targetItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                uint32 targetEntry = targetItem ? targetItem->GetEntry() : 0u;
                uint32 fakeEntry = targetItem ? sT->GetFakeEntry(targetItem->GetGUID()) : 0u;
                uint32 visualEntry = fakeEntry && fakeEntry != HIDDEN_ITEM_ID ? fakeEntry : targetEntry;

                std::string icon = visualEntry
                    ? sT->GetItemIcon(visualEntry, 30, 30, -18, 0)
                    : sT->GetSlotIcon(slot, 30, 30, -18, 0);
                std::string slotText = icon + std::string(slotName);
                bool staged = false;
                if (Transmogrification::outfitDraft const* draft = sT->GetOutfitDraft(player))
                    staged = draft->find(slot) != draft->end();
                if (staged)
                    slotText += " |cffffcc00- Preview staged|r";
                else if (fakeEntry)
                    slotText += " |cff00ff00- Select to replace current appearance|r";
                else if (!targetItem)
                    slotText += " |cff888888- Empty target slot|r";

                // RTG_Core renders the paper-doll, but mod-transmog owns the
                // authoritative slot state. Keep the transport marker hidden in
                // the raw gossip row so empty-slot detection, current visual,
                // and rarity lighting never depend on Scoreboard guesses or an
                // incompletely cached GetInventoryItem* client call.
                slotText += GetRtgTransmogSlotStateMarker(player, slot);

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, slotText, EQUIPMENT_SLOT_END, slot);
            }
        }
        if (Transmogrification::outfitDraft const* draft = sT->GetOutfitDraft(player))
        {
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                "|TInterface/ICONS/INV_Misc_Statue_02:30:30:-18:0|t|cffffcc00Review Outfit Preview|r ("
                    + std::to_string(draft->size()) + ")" + GetRtgOutfitSummaryMarker(player),
                TRANSMOG_OUTFIT_REVIEW_SENDER, 0);
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                "|TInterface/PaperDollInfoFrame/UI-GearManager-LeaveItem-Opaque:30:30:-18:0|tClear Outfit Preview",
                TRANSMOG_OUTFIT_CLEAR_SENDER, 0);
        }
#ifdef PRESETS
        if (sT->GetEnableSets())
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/RAIDFRAME/UI-RAIDFRAME-MAINASSIST:30:30:-18:0|t" + GetLocaleText(locale, "manage_sets"), EQUIPMENT_SLOT_END + 4, 0);
#endif
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
            "|TInterface/ICONS/INV_Misc_Book_09:30:30:-18:0|tCharacter Trophy Collection",
            TRANSMOG_TROPHY_SENDER, 1);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/INV_Enchant_Disenchant:30:30:-18:0|t" + GetLocaleText(locale, "remove_transmog"), EQUIPMENT_SLOT_END + 2, 0, GetLocaleText(locale, "remove_transmog_ask"), 0, false);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/PaperDollInfoFrame/UI-GearManager-Undo:30:30:-18:0|t" + GetLocaleText(locale, "update_menu"), EQUIPMENT_SLOT_END + 1, 0);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, GetTransmogMenuGuid(player, creature));
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (!player || !player->GetSession())
            return false;

        player->PlayerTalkClass->ClearMenus();
        WorldSession* session = player->GetSession();

        if (sender == TRANSMOG_SCOREBOARD_RETURN_SENDER)
        {
            OpenRTGScoreboardCosmetics(player);
            return true;
        }

        if (!sT->IsEnabled() || (!creature && !sT->IsPortableNPCEnabled))
        {
            ChatHandler(session).SendSysMessage("Portable Transmog is currently unavailable.");
            CloseGossipMenuFor(player);
            return true;
        }

        LocaleConstant locale = session->GetSessionDbLocaleIndex();

        if (sender == TRANSMOG_OUTFIT_REVIEW_SENDER)
        {
            ShowOutfitReview(player, creature);
            return true;
        }
        if (sender == TRANSMOG_OUTFIT_APPLY_SENDER)
        {
            std::string result;
            bool success = sT->ApplyOutfitDraft(player, result);
            if (success) session->SendAreaTriggerMessage("{}", result);
            else ChatHandler(session).SendSysMessage(result);
            OnGossipHello(player, creature);
            return true;
        }
        if (sender == TRANSMOG_OUTFIT_CLEAR_SENDER)
        {
            sT->ClearOutfitDraft(player);
            session->SendAreaTriggerMessage("Outfit preview cleared. Nothing was charged.");
            OnGossipHello(player, creature);
            return true;
        }
        if (sender == TRANSMOG_TROPHY_SENDER)
        {
            ShowTrophyCollection(player, creature, action ? action : 1);
            return true;
        }
        if (sender == TRANSMOG_TROPHY_SOURCE_SENDER)
        {
            ChatHandler(session).SendSysMessage("RTGATLAS^OPEN^" + std::to_string(action));
            return true;
        }

        if (sender == TRANSMOG_OUTFIT_CLEAN_SENDER)
        {
            if (!sT->GetEnableSets() || action >= sT->GetMaxSets())
            {
                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                return true;
            }
            auto ownerIdItr = sT->presetById.find(player->GetGUID());
            if (ownerIdItr == sT->presetById.end())
            {
                ChatHandler(session).SendSysMessage("That saved outfit no longer exists.");
                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                return true;
            }
            auto presetItr = ownerIdItr->second.find(uint8(action));
            if (presetItr == ownerIdItr->second.end())
            {
                ChatHandler(session).SendSysMessage("That saved outfit no longer exists.");
                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                return true;
            }

            std::vector<uint8> const unavailable = FindUnavailableSavedOutfitSlots(player, presetItr->second);
            if (unavailable.empty())
            {
                session->SendAreaTriggerMessage("Every saved slot is currently available.");
                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 6, action);
                return true;
            }
            if (unavailable.size() == presetItr->second.size())
            {
                ChatHandler(session).SendSysMessage("No usable slots remain in that saved outfit. Update or delete it instead.");
                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 6, action);
                return true;
            }

            for (uint8 slot : unavailable)
                presetItr->second.erase(slot);
            std::string const data = SerializeSavedOutfit(presetItr->second);
            CharacterDatabase.DirectExecute(
                "UPDATE `custom_transmogrification_sets` SET `SetData`='{}',`UpdatedAt`=UNIX_TIMESTAMP(),`DataVersion`=1 "
                "WHERE `Owner`={} AND `PresetID`={}",
                data, player->GetGUID().GetCounter(), action);
            session->SendAreaTriggerMessage("Removed {} unavailable slot{} from the saved outfit.",
                unavailable.size(), unavailable.size() == 1 ? "" : "s");
            OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 6, action);
            return true;
        }

        if (sender == TRANSMOG_OUTFIT_UPDATE_SENDER)
        {
            if (!sT->GetEnableSets() || action >= sT->GetMaxSets())
            {
                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                return true;
            }

            auto ownerIdItr = sT->presetById.find(player->GetGUID());
            auto ownerNameItr = sT->presetByName.find(player->GetGUID());
            if (ownerIdItr == sT->presetById.end() || ownerNameItr == sT->presetByName.end()
                || ownerIdItr->second.find(uint8(action)) == ownerIdItr->second.end()
                || ownerNameItr->second.find(uint8(action)) == ownerNameItr->second.end())
            {
                ChatHandler(session).SendSysMessage("That saved outfit no longer exists.");
                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                return true;
            }

            std::map<uint8, uint32> const items = BuildCompleteOutfitLook(player);
            if (items.empty())
            {
                ChatHandler(session).SendSysMessage("Equip at least one supported item before replacing a saved outfit.");
                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 6, action);
                return true;
            }

            std::string const data = SerializeSavedOutfit(items);
            ownerIdItr->second[uint8(action)] = items;
            CharacterDatabase.Execute(
                "UPDATE `custom_transmogrification_sets` SET `SetData`='{}',`UpdatedAt`=UNIX_TIMESTAMP(),`DataVersion`=1 "
                "WHERE `Owner`={} AND `PresetID`={}",
                data, player->GetGUID().GetCounter(), action);
            session->SendAreaTriggerMessage("Saved outfit updated from your complete current preview.");
            OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 6, action);
            return true;
        }

        // Next page
        if (sender > EQUIPMENT_SLOT_END + 10)
        {
            ShowTransmogItemsInGossipMenu(player, creature, action, sender);
            return true;
        }
        switch (sender)
        {
            case EQUIPMENT_SLOT_END: // Show items you can use
            {
                sT->selectionCache[player->GetGUID()] = action;

                bool useVendorInterface = player->GetPlayerSetting("mod-transmog", SETTING_VENDOR_INTERFACE).IsEnabled();
                bool allowVendorInterface = creature && (sT->GetUseVendorInterface() || useVendorInterface);

                if (allowVendorInterface)
                    ShowTransmogItemsInFakeVendor(player, creature, action);
                else
                    ShowTransmogItemsInGossipMenu(player, creature, action, sender);

                break;
            }
            case EQUIPMENT_SLOT_END + 1: // Main menu
                OnGossipHello(player, creature);
                break;
            case EQUIPMENT_SLOT_END + 2: // Remove Transmogrifications
            {
                bool removed = false;
                auto trans = CharacterDatabase.BeginTransaction();
                for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                {
                    if (Item* newItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    {
                        if (!sT->GetFakeEntry(newItem->GetGUID()))
                            continue;
                        sT->DeleteFakeEntry(player, slot, newItem, &trans);
                        removed = true;
                    }
                }
                if (removed)
                {
                    session->SendAreaTriggerMessage("{}", GTS(LANG_ERR_UNTRANSMOG_OK));
                    CharacterDatabase.CommitTransaction(trans);
                }
                else
                    ChatHandler(session).SendNotification(LANG_ERR_UNTRANSMOG_NO_TRANSMOGS);
                OnGossipHello(player, creature);
            } break;
            case EQUIPMENT_SLOT_END + 3: // Preview original appearance for one slot
            {
                std::string error;
                if (!sT->StageOutfitAppearance(player, uint8(action), 0, error))
                    ChatHandler(session).SendSysMessage(error);
                OnGossipHello(player, creature);
            } break;
    #ifdef PRESETS
            case EQUIPMENT_SLOT_END + 4: // Presets menu
            {
                if (!sT->GetEnableSets())
                {
                    OnGossipHello(player, creature);
                    return true;
                }
                if (sT->GetEnableSetInfo())
                    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/INV_Misc_Book_11:30:30:-18:0|t" + GetLocaleText(locale, "how_sets_work"), EQUIPMENT_SLOT_END + 10, 0);
                for (Transmogrification::presetIdMap::const_iterator it = sT->presetByName[player->GetGUID()].begin(); it != sT->presetByName[player->GetGUID()].end(); ++it)
                    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/INV_Misc_Statue_02:30:30:-18:0|t" + it->second, EQUIPMENT_SLOT_END + 6, it->first);

                if (sT->presetByName[player->GetGUID()].size() < sT->GetMaxSets())
                {
                    std::string saveSetText = "|TInterface/GuildBankFrame/UI-GuildBankFrame-NewTab:30:30:-18:0|t" + GetLocaleText(locale, "save_set");
                    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, saveSetText, EQUIPMENT_SLOT_END + 8, 0);
                }
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/Ability_Spy:30:30:-18:0|t" + GetLocaleText(locale, "back"), EQUIPMENT_SLOT_END + 1, 0);
                SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, GetTransmogMenuGuid(player, creature));
            } break;
            case EQUIPMENT_SLOT_END + 5: // Preview saved outfit
            {
                if (!sT->GetEnableSets() || action >= sT->GetMaxSets())
                {
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                    return true;
                }
                auto ownerItr = sT->presetById.find(player->GetGUID());
                if (ownerItr == sT->presetById.end())
                {
                    ChatHandler(session).SendSysMessage("That saved outfit no longer exists.");
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                    return true;
                }
                auto presetItr = ownerItr->second.find(uint8(action));
                if (presetItr == ownerItr->second.end())
                {
                    ChatHandler(session).SendSysMessage("That saved outfit no longer exists.");
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                    return true;
                }
                std::string error;
                if (!sT->StageSavedOutfit(player, presetItr->second, error))
                {
                    ChatHandler(session).SendSysMessage(error);
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 6, action);
                    return true;
                }
                ShowOutfitReview(player, creature);
            } break;
            case EQUIPMENT_SLOT_END + 6: // view preset
            {
                if (!sT->GetEnableSets())
                {
                    OnGossipHello(player, creature);
                    return true;
                }
                if (action >= sT->GetMaxSets())
                {
                    ChatHandler(session).SendSysMessage("That Saved Outfit is invalid.");
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                    return true;
                }

                auto ownerIdItr = sT->presetById.find(player->GetGUID());
                auto ownerNameItr = sT->presetByName.find(player->GetGUID());
                if (ownerIdItr == sT->presetById.end() || ownerNameItr == sT->presetByName.end())
                {
                    ChatHandler(session).SendSysMessage("That Saved Outfit no longer exists.");
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                    return true;
                }

                auto presetItr = ownerIdItr->second.find(uint8(action));
                auto presetNameItr = ownerNameItr->second.find(uint8(action));
                if (presetItr == ownerIdItr->second.end() || presetNameItr == ownerNameItr->second.end())
                {
                    ChatHandler(session).SendSysMessage("That Saved Outfit no longer exists.");
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                    return true;
                }

                for (auto const& [savedSlot, savedEntry] : presetItr->second)
                    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                        FormatSavedOutfitAppearance(player, savedSlot, savedEntry), sender, action);

                std::vector<uint8> const unavailable = FindUnavailableSavedOutfitSlots(player, presetItr->second);
                if (!unavailable.empty())
                    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                        "|TInterface/PaperDollInfoFrame/UI-GearManager-Undo:30:30:-18:0|tRemove "
                            + std::to_string(unavailable.size()) + " Unavailable Slot" + (unavailable.size() == 1 ? "" : "s"),
                        TRANSMOG_OUTFIT_CLEAN_SENDER, action,
                        "Remove only the unavailable slots from this character's saved outfit?", 0, false);

                std::string useSetText = "|TInterface/ICONS/INV_Misc_Statue_02:30:30:-18:0|tPreview Saved Outfit";
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, useSetText, EQUIPMENT_SLOT_END + 5, action);
                if (!BuildCompleteOutfitLook(player).empty())
                    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                        "|TInterface/PaperDollInfoFrame/UI-GearManager-Undo:30:30:-18:0|tReplace with Current Preview",
                        TRANSMOG_OUTFIT_UPDATE_SENDER, action,
                        "Replace this saved outfit with the character's complete current preview? The outfit name will stay the same.",
                        0, false);
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/PaperDollInfoFrame/UI-GearManager-LeaveItem-Opaque:30:30:-18:0|t" + GetLocaleText(locale, "delete_set"), EQUIPMENT_SLOT_END + 7, action, GetLocaleText(locale, "confirm_delete_set") + presetNameItr->second + "?", 0, false);
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/Ability_Spy:30:30:-18:0|t" + GetLocaleText(locale, "back"), EQUIPMENT_SLOT_END + 4, 0);
                SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, GetTransmogMenuGuid(player, creature));
            } break;
            case EQUIPMENT_SLOT_END + 7: // Delete preset
            {
                if (!sT->GetEnableSets())
                {
                    OnGossipHello(player, creature);
                    return true;
                }
                if (action >= sT->GetMaxSets())
                {
                    ChatHandler(session).SendSysMessage("That Saved Outfit is invalid.");
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                    return true;
                }

                uint8 const presetId = uint8(action);
                auto ownerIdItr = sT->presetById.find(player->GetGUID());
                auto ownerNameItr = sT->presetByName.find(player->GetGUID());
                bool const idExists = ownerIdItr != sT->presetById.end()
                    && ownerIdItr->second.find(presetId) != ownerIdItr->second.end();
                bool const nameExists = ownerNameItr != sT->presetByName.end()
                    && ownerNameItr->second.find(presetId) != ownerNameItr->second.end();
                if (!idExists && !nameExists)
                {
                    ChatHandler(session).SendSysMessage("That Saved Outfit no longer exists.");
                    OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
                    return true;
                }

                CharacterDatabase.Execute("DELETE FROM `custom_transmogrification_sets` WHERE Owner = {} AND PresetID = {}", player->GetGUID().GetCounter(), uint32(presetId));
                if (idExists)
                    ownerIdItr->second.erase(presetId);
                if (nameExists)
                    ownerNameItr->second.erase(presetId);

                OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
            } break;
            case EQUIPMENT_SLOT_END + 8: // Save outfit
            {
                if (!sT->GetEnableSets() || sT->presetByName[player->GetGUID()].size() >= sT->GetMaxSets())
                {
                    OnGossipHello(player, creature);
                    return true;
                }

                std::map<uint8, uint32> const items = BuildCompleteOutfitLook(player);
                for (auto const& [savedSlot, savedEntry] : items)
                    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                        FormatSavedOutfitAppearance(player, savedSlot, savedEntry), EQUIPMENT_SLOT_END + 8, 0);

                if (!items.empty())
                {
                    std::string insertName = "Name this character-specific outfit. Saving is free and does not apply any appearance.";
                    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                        "|TInterface/GuildBankFrame/UI-GuildBankFrame-NewTab:30:30:-18:0|tSave Complete Preview as Outfit",
                        EncodeTransmogCodeSender(0), 0, insertName, 0, true);
                }
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/PaperDollInfoFrame/UI-GearManager-Undo:30:30:-18:0|t" + GetLocaleText(locale, "update_menu"), sender, action);
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/Ability_Spy:30:30:-18:0|t" + GetLocaleText(locale, "back"), EQUIPMENT_SLOT_END + 4, 0);
                SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, GetTransmogMenuGuid(player, creature));
            } break;
            case EQUIPMENT_SLOT_END + 10: // Set info
            {
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/Ability_Spy:30:30:-18:0|t" + GetLocaleText(locale, "back"), EQUIPMENT_SLOT_END + 4, 0);
                SendGossipMenuFor(player, sT->GetSetNpcText(), GetTransmogMenuGuid(player, creature));
            } break;
    #endif
            case EQUIPMENT_SLOT_END + 9: // Transmog info
            {
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/Ability_Spy:30:30:-18:0|t" + GetLocaleText(locale, "back"), EQUIPMENT_SLOT_END + 1, 0);
                SendGossipMenuFor(player, sT->GetTransmogNpcText(), GetTransmogMenuGuid(player, creature));
            } break;
            default: // Transmogrify
            {
                if (!sender && !action)
                {
                    OnGossipHello(player, creature);
                    return true;
                }
                std::string error;
                if (!sT->StageOutfitAppearance(player, uint8(sender), action, error))
                    ChatHandler(session).SendSysMessage(error);
                else
                    session->SendAreaTriggerMessage("Appearance added to your outfit preview. Nothing has been charged.");
                OnGossipHello(player, creature);
            } break;
        }
        return true;
    }

#ifdef PRESETS
    bool OnGossipSelectCode(Player* player, Creature* creature, uint32 sender, uint32 action, const char* code) override
    {
        player->PlayerTalkClass->ClearMenus();
        sender = DecodeTransmogCodeSender(sender);
        if (sender)
        {
            // "sender" is an equipment slot for a search - execute the search
            std::string searchString(code);
            if (searchString.length() > MAX_SEARCH_STRING_LENGTH)
                searchString = searchString.substr(0, MAX_SEARCH_STRING_LENGTH);
            sT->searchStringByPlayer.erase(player->GetGUID().GetCounter());
            sT->searchStringByPlayer.insert({player->GetGUID().GetCounter(), searchString});
            OnGossipSelect(player, creature, EQUIPMENT_SLOT_END, sender - 1);
            return true;
        }
        if (action)
            return true; // should never happen
        if (!sT->GetEnableSets())
        {
            OnGossipHello(player, creature);
            return true;
        }
        std::string name(code);
        auto const firstVisible = std::find_if_not(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c) != 0; });
        auto const lastVisible = std::find_if_not(name.rbegin(), name.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
        if (firstVisible < lastVisible)
            name = std::string(firstVisible, lastVisible);
        else
            name.clear();

        if (name.empty() || name.size() > 32 || name.find('"') != std::string::npos || name.find('\\') != std::string::npos)
            ChatHandler(player->GetSession()).SendNotification("Outfit names must contain 1-32 visible characters and cannot use quotes or backslashes.");
        else
        {
            for (uint8 presetID = 0; presetID < sT->GetMaxSets(); ++presetID) // should never reach over max
            {
                if (sT->presetByName[player->GetGUID()].find(presetID) != sT->presetByName[player->GetGUID()].end())
                    continue; // Just remember never to use presetByName[pGUID][presetID] when finding etc!

                std::map<uint8, uint32> items = BuildCompleteOutfitLook(player);
                if (items.empty())
                {
                    ChatHandler(player->GetSession()).SendSysMessage("Preview or apply at least one appearance before saving an outfit.");
                    break;
                }

                std::string const serialized = SerializeSavedOutfit(items);
                for (auto const& [savedSlot, savedEntry] : items)
                    sT->presetById[player->GetGUID()][presetID][savedSlot] = savedEntry;
                sT->presetByName[player->GetGUID()][presetID] = name;
                std::string escapedName = name;
                CharacterDatabase.EscapeString(escapedName);
                CharacterDatabase.Execute(
                    "INSERT INTO `custom_transmogrification_sets` (`Owner`,`PresetID`,`SetName`,`SetData`,`CreatedAt`,`UpdatedAt`,`DataVersion`) "
                    "VALUES ({},{},'{}','{}',UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),1) "
                    "ON DUPLICATE KEY UPDATE `SetName`='{}',`SetData`='{}',`UpdatedAt`=UNIX_TIMESTAMP(),`DataVersion`=1",
                    player->GetGUID().GetCounter(), uint32(presetID), escapedName, serialized, escapedName, serialized);
                break;
            }
        }
        OnGossipSelect(player, creature, EQUIPMENT_SLOT_END + 4, 0);
        return true;
    }
#endif

    void ShowTransmogItemsInGossipMenu(Player* player, Creature* creature, uint8 slot, uint16 gossipPageNumber) // Only checks bags while can use an item from anywhere in inventory
    {
        WorldSession* session = player->GetSession();
        LocaleConstant locale = session->GetSessionDbLocaleIndex();
        Item* oldItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        bool hasSearchString = false;

        uint16 pageNumber = 0;
        uint32 startValue = 0;
        uint32 endValue = MAX_OPTIONS - 4;
        bool lastPage = true;
        if (gossipPageNumber > EQUIPMENT_SLOT_END + 10)
        {
            pageNumber = gossipPageNumber - EQUIPMENT_SLOT_END - 10;
            startValue = (pageNumber * (MAX_OPTIONS - 2));
            endValue = (pageNumber + 1) * (MAX_OPTIONS - 2) - 1;
        }

        if (oldItem)
        {
            uint32 existingTransmog = sT->GetFakeEntry(oldItem->GetGUID());
            uint32 price = GetTransmogPrice(oldItem->GetTemplate());
            bool freeTransmogReady = sT->HasFreeTransmogReady(player);
            std::ostringstream ss;
            ss << std::endl;
            if (sT->GetRequireToken() && !freeTransmogReady)
                ss << std::endl << std::endl << sT->GetTokenAmount() << " x " << sT->GetItemLink(sT->GetTokenEntry(), session);
            std::string lineEnd = ss.str();

            std::unordered_map<uint32, std::string>::iterator searchStringIterator = sT->searchStringByPlayer.find(player->GetGUID().GetCounter());
            hasSearchString = !(searchStringIterator == sT->searchStringByPlayer.end());
            std::string searchDisplayValue(hasSearchString ? searchStringIterator->second : GetLocaleText(locale, "search"));
            std::vector<ItemTemplate const*> allowedItems = GetValidTransmogs(player, oldItem, hasSearchString, searchDisplayValue);

            if (allowedItems.size() > 0)
            {
                lastPage = false;
                // Offset values to add Search gossip item
                if (pageNumber == 0)
                {
                    if (hasSearchString)
                    {
                        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, sT->GetItemIcon(30620, 30, 30, -18, 0) + GetLocaleText(locale, "searching_for") + searchDisplayValue, EncodeTransmogCodeSender(slot + 1), 0, GetLocaleText(locale, "search_for_item"), 0, true);
                    }
                    else
                    {
                        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, sT->GetItemIcon(30620, 30, 30, -18, 0) + GetLocaleText(locale, "search"), EncodeTransmogCodeSender(slot + 1), 0, GetLocaleText(locale, "search_for_item"), 0, true);
                    }
                }
                else
                {
                    startValue--;
                }
                if (sT->GetAllowHiddenTransmog() && existingTransmog != HIDDEN_ITEM_ID)
                {
                    // Offset the start and end values to make space for invisible item entry.
                    // Hiding can replace an existing visible transmog without removing it first.
                    endValue--;
                    if (pageNumber != 0)
                    {
                        startValue--;
                    }
                    else
                    {
                        // Add invisible item entry
                        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                            "|TInterface/ICONS/inv_misc_enggizmos_27:30:30:-18:0|tPreview Hidden Slot",
                            slot, UINT_MAX);
                    }
                }
                for (uint32 i = startValue; i <= endValue; i++)
                {
                    if (allowedItems.empty() || i > allowedItems.size() - 1)
                    {
                        lastPage = true;
                        break;
                    }
                    ItemTemplate const* newItem = allowedItems.at(i);
                    {
                        uint8 paymentType = sT->GetPaymentType(); // 0=Gold, 1=VP, 2=Gold+VP

                        uint32 const vpCost = paymentType == 0 ? 0u : GetTransmogVotePointPrice(price);

                        std::string lineText = sT->GetItemIcon(newItem->ItemId, 30, 30, -18, 0) + sT->GetItemLink(newItem->ItemId, session);
                        std::string confirmText = GetLocaleText(locale, "confirm_use_item") + sT->GetItemIcon(newItem->ItemId, 40, 40, -15, -10) + sT->GetItemLink(newItem->ItemId, session) + lineEnd;

                        bool isCurrentAppearance = existingTransmog == newItem->ItemId;
                        if (isCurrentAppearance)
                        {
                            lineText += "  |cff00ff00(Currently Applied)|r";
                            confirmText += "\n\n|cff00ff00This appearance is already applied. You will not be charged.|r";
                        }
                        else if (existingTransmog)
                            confirmText += "\n\n|cffffcc00This will replace the currently applied appearance.|r";

                        // Colored VP text
                        auto vpText = [&](uint32 vp) -> std::string
                        {
                            std::ostringstream os;
                            os << "|cff00ff00" << vp << " VP|r";
                            return os.str();
                        };

                        uint32 boxMoney = isCurrentAppearance ? 0u : price; // copper shown in UI (gold cost column)
                        bool paidTransmog = sT->GetRequireToken() || price > 0;

                        if (!isCurrentAppearance && freeTransmogReady && paidTransmog)
                        {
                            boxMoney = 0;
                            lineText += "  -  |cff00ff00FREE READY|r";
                            confirmText += "\n\n|cff00ff00Your free 90-minute transmog use will be consumed.|r";
                        }
                        else if (!isCurrentAppearance && paymentType == 1)
                        {
                            // VP-only: show no coin cost in the UI, display VP in the line text
                            boxMoney = 0;
                            if (vpCost > 0)
                            {
                                lineText += "  -  Cost: " + vpText(vpCost);
                                confirmText += "\n\nCost: " + vpText(vpCost);
                            }
                        }
                        else if (!isCurrentAppearance && paymentType == 2)
                        {
                            // Gold + VP: keep coin cost in UI, append VP to the line text
                            if (vpCost > 0)
                            {
                                lineText += "  +  " + vpText(vpCost);
                                confirmText += "\n\nVote Points: " + vpText(vpCost);
                            }
                        }

                        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, lineText + "  |cffffcc00— Preview|r", slot, newItem->ItemId);
                    }
                }
            }
            if (gossipPageNumber == EQUIPMENT_SLOT_END + 11)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GetLocaleText(locale, "previous_page"), EQUIPMENT_SLOT_END, slot);
                if (!lastPage)
                {
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, GetLocaleText(locale, "next_page"), gossipPageNumber + 1, slot);
                }
            }
            else if (gossipPageNumber > EQUIPMENT_SLOT_END + 11)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, GetLocaleText(locale, "previous_page"), gossipPageNumber - 1, slot);
                if (!lastPage)
                {
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, GetLocaleText(locale, "next_page"), gossipPageNumber + 1, slot);
                }
            }
            else if (!lastPage)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Next Page", EQUIPMENT_SLOT_END + 11, slot);
            }

            if (existingTransmog)
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/INV_Enchant_Disenchant:30:30:-18:0|tPreview Original Appearance", EQUIPMENT_SLOT_END + 3, slot);
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/PaperDollInfoFrame/UI-GearManager-Undo:30:30:-18:0|t" + GetLocaleText(locale, "update_menu"), EQUIPMENT_SLOT_END, slot);
        }
        else
        {
            std::string slotName = sT->GetSlotName(slot, session)
                ? sT->GetSlotName(slot, session)
                : "Equipment Slot";
            std::string emptyText = sT->GetSlotIcon(slot, 30, 30, -18, 0)
                + "|cffffcc00" + slotName + " is empty.|r\n"
                + "|cffb8aa92Equip an item in this slot before applying an appearance. "
                  "The slot remains visible in RTG Transmog and will refresh when an item is equipped.|r";
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, emptyText, EQUIPMENT_SLOT_END + 1, 0);
        }
        std::string backText = "|TInterface/ICONS/Ability_Spy:30:30:-18:0|t" + GetLocaleText(locale, "back");
        // Player-owned portable pages are asynchronous. RTG_Core reads this raw
        // slot marker to reject stale/mismatched responses, then strips it from
        // every visible label and dropdown row. Keep it on the return row so an
        // otherwise empty slot page can still be matched safely.
        if (!creature)
        {
            backText += " |cff010101[RTGTSLOT:" + std::to_string(uint32(slot + 1)) + "]|r";
            // Refresh the current slot's visual and quality after apply/remove
            // without forcing the player back through the root paper-doll page.
            backText += GetRtgTransmogSlotStateMarker(player, slot);
        }
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, backText, EQUIPMENT_SLOT_END + 1, 0);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, GetTransmogMenuGuid(player, creature));
    }

    static std::vector<ItemTemplate const*> GetSpoofedVendorItems (Item* target)
    {
        std::vector<ItemTemplate const*> spoofedItems;
        uint32 existingTransmog = sT->GetFakeEntry(target->GetGUID());
        // Hidden appearance can replace a visible transmog directly. Only suppress the
        // button when the slot is already hidden.
        if (sT->AllowHiddenTransmog && existingTransmog != HIDDEN_ITEM_ID)
        {
            ItemTemplate const* _hideSlotButton = sObjectMgr->GetItemTemplate(CUSTOM_HIDE_ITEM_VENDOR_ID);
            if (_hideSlotButton)
                spoofedItems.push_back(_hideSlotButton);
            else
            {
                _hideSlotButton = sObjectMgr->GetItemTemplate(FALLBACK_HIDE_ITEM_VENDOR_ID);
                spoofedItems.push_back(_hideSlotButton);
            }
        }
        if (existingTransmog)
        {
            ItemTemplate const* _removeTransmogButton = sObjectMgr->GetItemTemplate(CUSTOM_REMOVE_TMOG_VENDOR_ID);
            if (_removeTransmogButton)
                spoofedItems.push_back(_removeTransmogButton);
            else
            {
                _removeTransmogButton = sObjectMgr->GetItemTemplate(FALLBACK_REMOVE_TMOG_VENDOR_ID);
                spoofedItems.push_back(_removeTransmogButton);
            }
        }
        return spoofedItems;
    }

    static uint32 GetSpoofedItemPrice(Player* player, uint32 itemId, ItemTemplate const* target)
    {
        switch (itemId)
        {
            case CUSTOM_HIDE_ITEM_VENDOR_ID:
            case FALLBACK_HIDE_ITEM_VENDOR_ID:
            {
                uint32 hidePrice = sT->HiddenTransmogIsFree ? 0 : sT->GetSpecialPrice(target);
                if (hidePrice > 0 && sT->HasFreeTransmogReady(player))
                    return 0;
                return hidePrice;
            }
            default:
                return 0;
        }
    }

    static void EncodeItemToPacket (WorldPacket& data, ItemTemplate const* proto, uint8& slot, uint32 price)
    {
        data << uint32(slot + 1);
        data << uint32(proto->ItemId);
        data << uint32(proto->DisplayInfoID);
        data << int32 (-1); //Infinite Stock
        data << uint32(price);
        data << uint32(proto->MaxDurability);
        data << uint32(1);  //Buy Count of 1
        data << uint32(0);
        slot++;
    }

    //The actual vendor options are handled in the player script below, OnBeforeBuyItemFromVendor
    static void ShowTransmogItemsInFakeVendor (Player* player, Creature* creature, uint8 slot)
    {
        Item* targetItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!targetItem)
        {
            ChatHandler(player->GetSession()).SendNotification(LANG_ERR_TRANSMOG_MISSING_DEST_ITEM);
            CloseGossipMenuFor(player);
            return;
        }
        ItemTemplate const* targetTemplate = targetItem->GetTemplate();

        std::vector<ItemTemplate const*> itemList = GetValidTransmogs(player, targetItem, false, "");
        std::vector<ItemTemplate const*> spoofedItems = GetSpoofedVendorItems(targetItem);

        uint32 itemCount = itemList.size();
        uint32 spoofCount = spoofedItems.size();
        uint32 totalItems = itemCount + spoofCount;
        uint32 price = GetTransmogPrice(targetItem->GetTemplate());
        if (sT->HasFreeTransmogReady(player) && (sT->GetRequireToken() || price > 0))
            price = 0;

        WorldPacket data(SMSG_LIST_INVENTORY, 8 + 1 + totalItems * 8 * 4);
        data << uint64(creature->GetGUID().GetRawValue());

        uint8 count = 0;
        size_t count_pos = data.wpos();
        data << uint8(count);

        for (uint32 i = 0; i < spoofCount && count < MAX_VENDOR_ITEMS; ++i)
        {
            EncodeItemToPacket (
                data, spoofedItems[i], count,
                GetSpoofedItemPrice(player, spoofedItems[i]->ItemId, targetTemplate)
            );
        }
        uint32 existingTransmog = sT->GetFakeEntry(targetItem->GetGUID());
        for (uint32 i = 0; i < itemCount && count < MAX_VENDOR_ITEMS; ++i)
        {
            ItemTemplate const* _proto = itemList[i];
            if (_proto)
            {
                uint32 itemPrice = _proto->ItemId == existingTransmog ? 0u : price;
                EncodeItemToPacket(data, _proto, count, itemPrice);
            }
        }

        data.put(count_pos, count);
        player->GetSession()->SendPacket(&data);
    }
};

class PS_Transmogrification : public PlayerScript
{
private:
    void AddToDatabase(Player* player, Item* item, std::string const& source)
    {
        if (item->HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_BOP_TRADEABLE) && !sTransmogrification->GetAllowTradeable())
            return;
        if (item->HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_REFUNDABLE))
            return;
        ItemTemplate const* itemTemplate = item->GetTemplate();
        AddToDatabase(player, itemTemplate, source);
    }

    void AddToDatabase(Player* player, ItemTemplate const* itemTemplate, std::string source)
    {
        LocaleConstant locale = player->GetSession()->GetSessionDbLocaleIndex();
        if (!sT->GetTrackUnusableItems() && !sT->SuitableForTransmogrification(player, itemTemplate))
            return;
        if (itemTemplate->Class != ITEM_CLASS_ARMOR && itemTemplate->Class != ITEM_CLASS_WEAPON)
            return;
        uint32 itemId    = itemTemplate->ItemId;
        uint32 accountId = player->GetSession()->GetAccountId();
        uint32 ownerGuid = player->GetGUID().GetCounter();
        std::string itemName = itemTemplate -> Name1;

        // get locale item name
        int loc_idex = player->GetSession()->GetSessionDbLocaleIndex();
        if (ItemLocale const* il = sObjectMgr->GetItemLocale(itemId))
            ObjectMgr::GetLocaleString(il->Name, loc_idex, itemName);

        std::stringstream tempStream;
        tempStream << std::hex << ItemQualityColors[itemTemplate->Quality];
        std::string itemQuality = tempStream.str();
        bool showChatMessage = !(player->GetPlayerSetting("mod-transmog", SETTING_HIDE_TRANSMOG).value) && !sT->CanNeverTransmog(itemTemplate);
        if (sT->AddCollectedAppearance(ownerGuid, itemId))
        {
            if (showChatMessage)
                ChatHandler(player->GetSession()).PSendSysMessage( R"(|c{}|Hitem:{}:0:0:0:0:0:0:0:0|h[{}]|h|r {})", itemQuality, itemId, itemName, GetLocaleText(locale, "added_appearance"));

            CharacterDatabase.EscapeString(source);
            CharacterDatabase.Execute(
                "INSERT INTO custom_unlocked_appearances "
                "(account_id, owner_guid, item_template_id, discovered_at, first_discovered_at, last_discovered_at, discovery_source, legacy_discovery) "
                "VALUES ({}, {}, {}, UNIX_TIMESTAMP(), UNIX_TIMESTAMP(), UNIX_TIMESTAMP(), '{}', 0) "
                "ON DUPLICATE KEY UPDATE `account_id`={},`last_discovered_at`=UNIX_TIMESTAMP(),"
                "`discovery_source`=IF(`legacy_discovery`=1,'{}',`discovery_source`),`legacy_discovery`=0",
                accountId, ownerGuid, itemId, source, accountId, source);
        }
    }

    void CheckRetroActiveOwnedAppearances(Player* player)
    {
        if (!player) return;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                AddToDatabase(player, item, "retroactive-owned");
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                AddToDatabase(player, item, "retroactive-owned");
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = player->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = player->GetItemByPos(bagSlot, slot))
                        AddToDatabase(player, item, "retroactive-owned");
        for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                AddToDatabase(player, item, "retroactive-bank");
        for (uint8 bagSlot = BANK_SLOT_BAG_START; bagSlot < BANK_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = player->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = player->GetItemByPos(bagSlot, slot))
                        AddToDatabase(player, item, "retroactive-bank");
        player->UpdatePlayerSetting("mod-transmog", SETTING_RETROACTIVE_CHECK, 1);
    }

public:
    PS_Transmogrification() : PlayerScript("Player_Transmogrify", {
        PLAYERHOOK_ON_EQUIP,
        PLAYERHOOK_ON_LOOT_ITEM,
        PLAYERHOOK_ON_CREATE_ITEM,
        PLAYERHOOK_ON_AFTER_STORE_OR_EQUIP_NEW_ITEM,
        PLAYERHOOK_ON_AFTER_SET_VISIBLE_ITEM_SLOT,
        PLAYERHOOK_ON_AFTER_MOVE_ITEM_FROM_INVENTORY,
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_BEFORE_BUY_ITEM_FROM_VENDOR
    }) { }

    void OnPlayerEquip(Player* player, Item* it, uint8 /*bag*/, uint8 /*slot*/, bool /*update*/) override
    {
        if (!sT->GetUseCollectionSystem())
            return;
        AddToDatabase(player, it, "equip");
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/) override
    {
        if (!sT->GetUseCollectionSystem() || !item || typeid(*item) != typeid(Item))
            return;
        if (item->GetTemplate()->Bonding == ItemBondingType::BIND_WHEN_PICKED_UP || item->IsSoulBound())
        {
            AddToDatabase(player, item, "loot");
        }
    }

    void OnPlayerCreateItem(Player* player, Item* item, uint32 /*count*/) override
    {
        if (!sT->GetUseCollectionSystem())
            return;
        if (item->GetTemplate()->Bonding == ItemBondingType::BIND_WHEN_PICKED_UP || item->IsSoulBound())
        {
            AddToDatabase(player, item, "crafted");
        }
    }

    void OnPlayerAfterStoreOrEquipNewItem(Player* player, uint32 /*vendorslot*/, Item* item, uint8 /*count*/, uint8 /*bag*/, uint8 /*slot*/, ItemTemplate const* /*pProto*/, Creature* pVendor, VendorItem const* /*crItem*/, bool /*bStore*/) override
    {
        if (!sT->GetUseCollectionSystem())
            return;
        if (item->GetTemplate()->Bonding == ItemBondingType::BIND_WHEN_PICKED_UP || item->IsSoulBound())
            AddToDatabase(player, item, pVendor ? "purchase" : "acquired");
    }


    void OnPlayerAfterSetVisibleItemSlot(Player* player, uint8 slot, Item *item) override
    {
        if (!item)
            return;

        if (uint32 entry = sT->GetFakeEntry(item->GetGUID()))
        {
            player->SetUInt32Value(PLAYER_VISIBLE_ITEM_1_ENTRYID + (slot * 2), entry);
        }
    }

    void OnPlayerAfterMoveItemFromInventory(Player* /*player*/, Item* it, uint8 /*bag*/, uint8 /*slot*/, bool /*update*/) override
    {
        sT->DeleteFakeFromDB(it->GetGUID().GetCounter());
    }

    void OnPlayerLogin(Player* player) override
    {
        if (sT->EnableResetRetroActiveAppearances())
            player->UpdatePlayerSetting("mod-transmog", SETTING_RETROACTIVE_CHECK, 0);

        if (sT->EnableRetroActiveAppearances() && !(player->GetPlayerSetting("mod-transmog", SETTING_RETROACTIVE_CHECK).value))
            CheckRetroActiveOwnedAppearances(player);

        ObjectGuid playerGUID = player->GetGUID();
        sT->entryMap.erase(playerGUID);
        QueryResult result = CharacterDatabase.Query("SELECT GUID, FakeEntry FROM custom_transmogrification WHERE Owner = {}", player->GetGUID().GetCounter());
        if (result)
        {
            do
            {
                ObjectGuid itemGUID = ObjectGuid::Create<HighGuid::Item>((*result)[0].Get<uint32>());
                uint32 fakeEntry = (*result)[1].Get<uint32>();
                if (fakeEntry == HIDDEN_ITEM_ID || sObjectMgr->GetItemTemplate(fakeEntry))
                {
                    sT->dataMap[itemGUID] = playerGUID;
                    sT->entryMap[playerGUID][itemGUID] = fakeEntry;
                }
            } while (result->NextRow());

            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    player->SetVisibleItemSlot(slot, item);
            }
        }

#ifdef PRESETS
        if (sT->GetEnableSets())
            sT->LoadPlayerSets(playerGUID);
#endif
    }

    void OnPlayerLogout(Player* player) override
    {
        sT->ClearOutfitDraft(player);
        ObjectGuid pGUID = player->GetGUID();
        for (Transmogrification::transmog2Data::const_iterator it = sT->entryMap[pGUID].begin(); it != sT->entryMap[pGUID].end(); ++it)
            sT->dataMap.erase(it->first);
        sT->entryMap.erase(pGUID);
        sT->selectionCache.erase(pGUID);

#ifdef PRESETS
        if (sT->GetEnableSets())
            sT->UnloadPlayerSets(pGUID);
#endif
    }

    void OnPlayerBeforeBuyItemFromVendor(Player* player, ObjectGuid vendorguid, uint32 /*vendorslot*/, uint32& itemEntry, uint8 /*count*/, uint8 /*bag*/, uint8 /*slot*/) override
    {
        Creature* vendor = player->GetMap()->GetCreature(vendorguid);
        if (!vendor)
            return;

        if (!sT->IsTransmogVendor(vendor->GetEntry()))
            return;

        uint8 slot = sT->selectionCache[player->GetGUID()];

        if (itemEntry == CUSTOM_HIDE_ITEM_VENDOR_ID || itemEntry == FALLBACK_HIDE_ITEM_VENDOR_ID)
        {
            PerformTransmogrification(player, UINT_MAX, 0);
        }
        else if (itemEntry == CUSTOM_REMOVE_TMOG_VENDOR_ID || itemEntry == FALLBACK_REMOVE_TMOG_VENDOR_ID)
        {
            RemoveTransmogrification(player);
        }
        else
        {
            PerformTransmogrification(player, itemEntry, 0);
        }
        npc_transmogrifier::ShowTransmogItemsInFakeVendor(player, vendor, slot); //Refresh menu
        itemEntry = 0; //Prevents the handler from proceeding to core vendor handling
    }
};

class WS_Transmogrification : public WorldScript
{
public:
    WS_Transmogrification() : WorldScript("WS_Transmogrification", {
        WORLDHOOK_ON_STARTUP
    }) { }

    void OnStartup() override
    {
        sT->LoadConfig(false);
        //sLog->outInfo(LOG_FILTER_SERVER_LOADING, "Deleting non-existing transmogrification entries...");
        CharacterDatabase.Execute("DELETE FROM custom_transmogrification WHERE NOT EXISTS (SELECT 1 FROM item_instance WHERE item_instance.guid = custom_transmogrification.GUID)");

#ifdef PRESETS
        // Clean even if disabled
        // Dont delete even if player has more presets than should
        CharacterDatabase.Execute("DELETE FROM `custom_transmogrification_sets` WHERE NOT EXISTS(SELECT 1 FROM characters WHERE characters.guid = custom_transmogrification_sets.Owner)");
#endif

        sT->LoadCollections();
    }
};

class global_transmog_script : public GlobalScript
{
public:
    global_transmog_script() : GlobalScript("global_transmog_script", {
        GLOBALHOOK_ON_ITEM_DEL_FROM_DB,
        GLOBALHOOK_ON_MIRRORIMAGE_DISPLAY_ITEM
    }) { }

    void OnItemDelFromDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType itemGuid) override
    {
        sT->DeleteFakeFromDB(itemGuid, &trans);
    }

    void OnMirrorImageDisplayItem(const Item *item, uint32 &display) override
    {
        if (uint32 entry = sTransmogrification->GetFakeEntry(item->GetGUID()))
        {
            if (entry == HIDDEN_ITEM_ID)
            {
                display = 0;
            }
            else
            {
                display=uint32(sObjectMgr->GetItemTemplate(entry)->DisplayInfoID);
            }
        }
    }
};

class unit_transmog_script : public UnitScript
{
public:
    unit_transmog_script() : UnitScript("unit_transmog_script", true, {
        UNITHOOK_SHOULD_TRACK_VALUES_UPDATE_POS_BY_INDEX,
        UNITHOOK_ON_PATCH_VALUES_UPDATE
    }) { }

    bool ShouldTrackValuesUpdatePosByIndex(Unit const* unit, uint8 /*updateType*/, uint16 index) override
    {
        return unit->IsPlayer() && index >= PLAYER_VISIBLE_ITEM_1_ENTRYID && index <= PLAYER_VISIBLE_ITEM_19_ENTRYID && (index & 1);
    }

    void OnPatchValuesUpdate(Unit const* unit, ByteBuffer& valuesUpdateBuf, BuildValuesCachePosPointers& posPointers, Player* target) override
    {
        if (!unit->IsPlayer())
            return;

        for (auto it = posPointers.other.begin(); it != posPointers.other.end(); ++it)
        {
            uint16 index = it->first;
            if (index >= PLAYER_VISIBLE_ITEM_1_ENTRYID && index <= PLAYER_VISIBLE_ITEM_19_ENTRYID && (index & 1))
                if (Item* item = unit->ToPlayer()->GetItemByPos(INVENTORY_SLOT_BAG_0, ((index - PLAYER_VISIBLE_ITEM_1_ENTRYID) / 2U)))
                    if (!sTransmogrification->IsEnabled() || target->GetPlayerSetting("mod-transmog", SETTING_HIDE_TRANSMOG).value)
                        valuesUpdateBuf.put(it->second, item->GetEntry());
        }
    }
};

class PlayerGossip_TransmogService final : public PlayerGossip
{
public:
    enum Senders
    {
        ROOT = 1000,
        DIRECT_SLOT = 2000,
		EXTENDED_INPUT_BASE = TRANSMOG_GOSSIP_EXTENDED_BASE,
		LEGACY_SENDER_MAX = 255,
		EXTENDED_INPUT_MAX = TRANSMOG_GOSSIP_EXTENDED_BASE + 100
    };

    PlayerGossip_TransmogService() : PlayerGossip(91013)
    {
        RegisterAction(ROOT, OpenRoot);
        RegisterAction(DIRECT_SLOT, OpenDirectSlot);

        // Bridge the legacy npc_transmogrifier gossip senders into PlayerGossip so
        // scoreboard and other player-opened entry points can reuse the original menu.
        for (uint32 sender = 0; sender <= LEGACY_SENDER_MAX; ++sender)
            RegisterAction(sender, DispatchSelect);

    #ifdef PRESETS
        for (uint32 sender = EXTENDED_INPUT_BASE; sender <= EXTENDED_INPUT_MAX; ++sender)
            RegisterExtendedAction(sender, DispatchSelectCode);
    #endif
    }

    static void OpenRoot(Player* player, int32, int32, std::any)
    {
        if (!player || !player->GetSession())
            return;

        npc_transmogrifier script;
        script.OnGossipHello(player, nullptr);
    }

    static void OpenDirectSlot(Player* player, int32, int32 action, std::any)
    {
        if (!player || !player->GetSession())
            return;

        if (action < 1 || action > EQUIPMENT_SLOT_END)
        {
            OpenRoot(player, 0, 0, std::any{});
            return;
        }

        uint8 equipmentSlot = uint8(action - 1);
        WorldSession* session = player->GetSession();
        if (!session || !sT->GetSlotName(equipmentSlot, session))
        {
            OpenRoot(player, 0, 0, std::any{});
            return;
        }

        // A direct slot request is a fresh browse operation. Never inherit the
        // previous slot's search string or cached inventory filter.
        sT->searchStringByPlayer.erase(player->GetGUID().GetCounter());

        npc_transmogrifier script;
        script.OnGossipSelect(player, nullptr, EQUIPMENT_SLOT_END, equipmentSlot);
    }

    static void DispatchSelect(Player* player, int32 sender, int32 action, std::any)
    {
        if (!player || !player->GetSession())
            return;

        npc_transmogrifier script;
        script.OnGossipSelect(player, nullptr, uint32(sender), uint32(action));
    }

    static void DispatchSelectCode(Player* player, int32 sender, int32 action, std::string code, std::any)
    {
        if (!player || !player->GetSession())
            return;

        npc_transmogrifier script;
        script.OnGossipSelectCode(player, nullptr, uint32(sender), uint32(action), code.c_str());
    }
};

namespace RTG::Services::Transmog
{
    bool IsAvailable()
    {
        return sT->IsEnabled() && sT->IsPortableNPCEnabled;
    }

    bool Open(Player* player)
    {
        if (!player || !player->GetSession() || !IsAvailable())
            return false;

        player->PlayerTalkClass->ClearMenus();
        CloseGossipMenuFor(player);

        sPlayerGossipMgr->ShowGossipMenu(player, 91013, PlayerGossip_TransmogService::ROOT, 0);
        return true;
    }

    bool OpenSlot(Player* player, uint8 inventorySlot)
    {
        if (!player || !player->GetSession() || !IsAvailable())
            return false;

        if (inventorySlot < 1 || inventorySlot > EQUIPMENT_SLOT_END)
            return Open(player);

        uint8 equipmentSlot = inventorySlot - 1;
        WorldSession* session = player->GetSession();
        if (!session || !sT->GetSlotName(equipmentSlot, session))
            return Open(player);

        player->PlayerTalkClass->ClearMenus();
        CloseGossipMenuFor(player);

        // The PlayerGossip service owns sender registration for all subsequent
        // appearance, paging, remove, hide, and Back actions. Opening the slot
        // through a dedicated service sender avoids replaying a stale root menu.
        sPlayerGossipMgr->ShowGossipMenu(
            player, 91013, PlayerGossip_TransmogService::DIRECT_SLOT, inventorySlot);
        return true;
    }
}

void AddSC_Transmog()
{
    new global_transmog_script();
    new unit_transmog_script();
    new npc_transmogrifier();
    new PS_Transmogrification();
    new WS_Transmogrification();
    new PlayerGossip_TransmogService();
}
