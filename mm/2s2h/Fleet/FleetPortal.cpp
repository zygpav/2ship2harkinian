#include "FleetPortal.h"
#include "FleetGame.h"
#include "FleetPassages.h"
#include "FleetRuntime.h"

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/CustomMessage/CustomMessage.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/Rando/Rando.h"
#include "2s2h/SaveManager/SaveManager.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/ModApi/Forms/FormRegistry.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "variables.h"
#include "z_title_setup.h"
#include "overlays/gamestates/ovl_file_choose/z_file_select.h"
}

namespace {

constexpr const char* kMarketPortal = "market";
// "Link" in the file select font: capitals from 0x0A, small letters from 0x24, 0x3E pads.
constexpr u8 kDefaultPlayerName[8] = { 0x15, 0x2C, 0x31, 0x2E, 0x3E, 0x3E, 0x3E, 0x3E };

struct Arrival {
    s32 entrance;
    s32 slot;
    bool cross = false;
    // Fleet Combo: the Link who crossed: "child"/"adult" and the form he wore (nei.* key).
    std::string age;
    std::string form;
};

std::optional<std::string> sDeparture;
// Fleet Combo: the "Soar to where?" prompt is open (Song of Soaring in Termina).
bool sSoaringPrompt = false;
// Fleet Combo: a registered form (adult Link) to put on once the arrival scene has a player; empty = none.
std::optional<std::string> sPendingForm;

// Fleet Combo: the doors (Mask Shop / Clock Tower) only travel when this is on; the songs do the travelling.
bool DoorPortalsEnabled() {
    return CVarGetInteger("gFleetCombo.DoorPortals", 0) != 0;
}
std::optional<Arrival> sArrival;

std::optional<s32> EntranceForPortal(const std::string& portal) {
    if (portal == kMarketPortal) {
        return ENTRANCE(SOUTH_CLOCK_TOWN, 0);
    }
    return FleetPassages_Entrance(portal);
}

bool IsPlaying() {
    return gPlayState != nullptr && gGameState == &gPlayState->state && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

void Depart(const std::string& portal) {
    if (FleetRuntime_HasSession()) {
        FleetRuntime_Save();
    }
    const char* form = FormRegistry_GetCurrentKey();
    const std::string payload = nlohmann::json{ { "portal", portal },
                                                { "slot", gSaveContext.fileNum },
                                                { "age", FormRegistry_IsCurrentFormAdult() ? "adult" : "child" },
                                                { "form", form != nullptr ? form : "" } }
                                    .dump();
    const FleetDestination destination = { .game = nullptr, .entrance = 0, .payloadJson = payload.c_str() };
    FleetGame_RequestSwitch(&destination);
}

// Only the owl flag of the file select is read, so a blank one opens the slot's cycle save.
void OpenSave() {
    FileSelectState* fileSelect = static_cast<FileSelectState*>(std::calloc(1, sizeof(FileSelectState)));
    nlohmann::json saveFile;
    if (SaveManager_ReadSaveFile(SaveManager_GetFileName(gSaveContext.fileNum + 1), saveFile) != 0) {
        std::free(fileSelect);
        throw std::runtime_error("cannot open MM Fleet save");
    }
    fileSelect->isOwlSave[gSaveContext.fileNum + FILE_NUM_OWL_SAVE_OFFSET] =
        saveFile.is_object() && saveFile.contains("owlSave");
    std::vector<u8> buffer(SAVE_BUFFER_SIZE);
    SramContext sram = {};
    sram.saveBuf = buffer.data();
    Sram_OpenSave(fileSelect, &sram);
    std::free(fileSelect);
}

// The first arrival can find 2ship outside gameplay: it opens the slot Fleet names, or starts a new file there.
void BootIntoPlay(const Arrival& arrival) {
    FleetRuntime_Initialize();
    // Fleet Combo: the title screen is what turns saving on (Title_Init). Arriving straight into gameplay skips it,
    // which left Play_Init without a save buffer and crashed the first save (entering Clock Town).
    gSaveContext.flashSaveAvailable = true;
    gSaveContext.fileNum = arrival.slot;
    bool fresh = false;
    if (SaveManager_SlotExists(arrival.slot)) {
        OpenSave();
    } else {
        fresh = true;
        FleetAutomaticSave automatic;
        Sram_InitNamedSave(arrival.slot, kDefaultPlayerName);
        if (FleetRuntime_HasSession() && !IS_RANDO) {
            throw std::runtime_error("MM could not create the combined randomizer save");
        }
        FleetRuntime_Save();
    }
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    if (FleetRuntime_HasSession() && (!IS_RANDO || gSaveContext.save.shipSaveInfo.rando.finalSeed !=
                                                       FleetRuntime_Seed().at("masterSeed").get<uint32_t>())) {
        throw std::runtime_error("MM save does not belong to this Fleet seed");
    }
    Sram_PrepareLoadedSave();
    if (fresh) {
        // Fleet Combo: a new Termina file starts with MM's own opening (Skull Kid, the Deku curse, the first cycle)
        // instead of dropping human Link in Clock Town. Undo the randomizer's first-cycle skip for this file.
        if (IS_RANDO) {
            gSaveContext.save.hasTatl = false;
            CLEAR_WEEKEVENTREG(WEEKEVENTREG_59_04);
            CLEAR_WEEKEVENTREG(WEEKEVENTREG_31_04);
            gSaveContext.save.saveInfo.permanentSceneFlags[SCENE_INSIDETOWER].switch0 &= ~(1 << 0);
            gSaveContext.save.saveInfo.playerData.threeDayResetCount = 0;
            SET_EQUIP_VALUE(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
            BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_B) = ITEM_SWORD_KOKIRI;
            SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_HERO);
        }
        gSaveContext.save.playerForm = PLAYER_FORM_HUMAN;
        gSaveContext.save.isFirstCycle = true;
        gSaveContext.save.day = 0;
        gSaveContext.save.time = CLOCK_TIME(6, 0) - 1;
        gSaveContext.respawnFlag = 0;
        gSaveContext.nextCutsceneIndex = 0xFFEF;
        gSaveContext.save.cutsceneIndex = 0xFFF0;
        gSaveContext.save.entrance = ENTRANCE(CUTSCENE, 0);
        gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    } else if (!arrival.age.empty()) {
        // Fleet Combo: arrive as the Link who left Hyrule: MM's own forms by their key, adult Link as a form.
        s32 nativeForm = -1;
        for (s32 form = 0; form < PLAYER_FORM_MAX; form++) {
            if (form == PLAYER_FORM_HUMAN) {
                continue; // human: child or adult Link, decided by age
            }
            const char* key = FormRegistry_GetNativeKey(static_cast<u8>(form));
            if (key != nullptr && arrival.form == key) {
                nativeForm = form;
            }
        }
        // Only keep a form whose mask this world has: Link takes a form off with its mask.
        static const ItemId kFormMasks[PLAYER_FORM_MAX] = { ITEM_MASK_FIERCE_DEITY, ITEM_MASK_GORON, ITEM_MASK_ZORA,
                                                            ITEM_MASK_DEKU, ITEM_NONE };
        if (nativeForm >= 0 && (kFormMasks[nativeForm] == ITEM_NONE ||
                                INV_CONTENT(kFormMasks[nativeForm]) != kFormMasks[nativeForm])) {
            nativeForm = -1;
        }
        gSaveContext.save.playerForm = nativeForm >= 0 ? static_cast<u8>(nativeForm) : PLAYER_FORM_HUMAN;
        sPendingForm = (nativeForm < 0 && arrival.age == "adult") ? std::string(S2H_FORM_KEY_ADULT) : std::string{};
    }
    if (!fresh && arrival.entrance >= 0) {
        if (arrival.cross && !FleetRuntime_QueueArrival(arrival.entrance)) {
            throw std::runtime_error("MM rejected an arrival absent from this seed");
        }
        gSaveContext.respawnFlag = 0;
        gSaveContext.nextCutsceneIndex = 0xFFEF;
        gSaveContext.save.cutsceneIndex = 0;
        gSaveContext.save.entrance =
            arrival.cross ? FleetRuntime_ConsumeArrival() : FleetRuntime_Materialize(arrival.entrance);
        gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    }
    STOP_GAMESTATE(gGameState);
    SET_NEXT_GAMESTATE(gGameState, Play_Init, sizeof(PlayState));
}

void EnterArrival() {
    if (gGameState != nullptr && gGameState->running) {
        // Setup reloads natively first; the composed destination is consumed after that load.
        STOP_GAMESTATE(gGameState);
        SET_NEXT_GAMESTATE(gGameState, Setup_Init, sizeof(SetupState));
    }
}

void RegisterPortalHooks() {
    if (!FleetGame_IsHosted()) {
        return;
    }
    FleetRuntime_Initialize();
    if (FleetRuntime_HasSession() && !sArrival) {
        sArrival = Arrival{ -1, FleetRuntime_Slot() };
    }
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](s16 sceneId, s8 spawnNum) {
        FleetRuntime_ObserveScene(sceneId);
        if (sPendingForm && gSaveContext.gameMode == GAMEMODE_NORMAL) {
            FormRegistry_ApplyActive(sPendingForm->empty() ? nullptr : sPendingForm->c_str());
            sPendingForm.reset();
        }
        // Spawn 1 is the door from South Clock Town; the cycle resets land on the tower's other spawns.
        if (sceneId == SCENE_INSIDETOWER && spawnNum == 1 && gSaveContext.gameMode == GAMEMODE_NORMAL &&
            DoorPortalsEnabled()) {
            sDeparture = kMarketPortal;
        }
    });
}

static RegisterShipInitFunc sPortalInit(RegisterPortalHooks, {});

} // namespace

extern "C" void FleetPortal_RequestDeparture(const char* portal) {
    sDeparture = portal;
}

extern "C" bool FleetPortal_LoadPending(void) {
    if (!sArrival) {
        return false;
    }
    BootIntoPlay(*sArrival);
    sArrival.reset();
    return true;
}

extern "C" void FleetPortal_Arrive(const FleetDestination* arrival) {
    if (arrival == nullptr || arrival->payloadJson == nullptr) {
        return;
    }
    const nlohmann::json payload = nlohmann::json::parse(arrival->payloadJson, nullptr, false);
    if (!payload.is_object()) {
        return;
    }
    const int slot = payload.value("slot", 0);
    if (slot < 0 || slot > 2) {
        return;
    }
    if (payload.value("session", false)) {
        sArrival = Arrival{ -1, slot };
    } else if (payload.value("cross", false)) {
        sArrival = Arrival{ payload.at("entrance").get<s32>(), slot, true };
    } else if (const std::optional<s32> entrance = EntranceForPortal(payload.value("portal", ""))) {
        sArrival = Arrival{ .entrance = *entrance, .slot = slot };
        sArrival->age = payload.value("age", "");
        sArrival->form = payload.value("form", "");
    }
    if (sArrival) {
        EnterArrival();
    }
}

// Fleet Combo: Song of Soaring in Termina asks where to fly. z_message.c calls this where the owl map would open.
extern "C" bool FleetCombo_SoaringPrompt(PlayState* play) {
    if (!FleetGame_IsHosted() || play == nullptr) {
        return false;
    }
    CustomMessage::Entry entry;
    entry.msg = "Soar to where?\x11\xC2%gHyrule\x11Termina owl statues%w";
    CustomMessage::ReplaceColorChars(&entry.msg);
    CustomMessage::StartTextbox(entry.msg, entry);
    sSoaringPrompt = true;
    return true;
}

extern "C" void Message_OpenSoaringOwlMap(PlayState* play);

void UpdateSoaringPrompt() {
    if (!sSoaringPrompt || gPlayState == nullptr) {
        return;
    }
    MessageContext* msgCtx = &gPlayState->msgCtx;
    const u8 state = Message_GetState(msgCtx);
    if (state == TEXT_STATE_NONE) {
        sSoaringPrompt = false; // closed some other way
        return;
    }
    if (state != TEXT_STATE_CHOICE || !Message_ShouldAdvance(gPlayState)) {
        return;
    }
    sSoaringPrompt = false;
    const u8 choice = msgCtx->choiceIndex;
    Audio_PlaySfx_MessageDecide();
    Message_CloseTextbox(gPlayState);
    msgCtx->ocarinaMode = OCARINA_MODE_END;
    if (choice == 0) {
        sDeparture = kMarketPortal; // lands in Castle Town Market, outside the Mask Shop
    } else if (gSaveContext.save.saveInfo.playerData.owlActivationFlags != 0) {
        Message_OpenSoaringOwlMap(gPlayState);
    }
}

extern "C" void FleetPortal_FrameEnd(void) {
    UpdateSoaringPrompt();
    FleetDestination destination = {};
    std::string payload;
    if (FleetRuntime_TakeDeparture(destination, payload)) {
        FleetGame_RequestSwitch(&destination);
        return;
    }
    if (sDeparture) {
        const std::string portal = *sDeparture;
        sDeparture.reset();
        Depart(portal);
    }
    if (sArrival) {
        EnterArrival();
    }
}
