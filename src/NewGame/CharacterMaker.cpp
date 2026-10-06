/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "CharacterMaker.h"

#include "CharacterCache.h"
#include "DBCStores.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "WorldSession.h"
#include <memory>
#include <utility>
#include <vector>

uint32 CharacterMaker::Create(uint32 account, std::string const& name, uint8 race, uint8 cls, uint8 gender,
                              std::string& error)
{
    if (!sObjectMgr->GetPlayerInfo(race, cls))
    {
        error = "race and class don't match: " + name;
        return 0;
    }
    if (gender > 1)
        gender = urand(0, 1) ? GENDER_FEMALE : GENDER_MALE;

    // Random looks from the race and gender's character sections, as RandomPlayerbotFactory::CreateRandomBot picks
    // them (the skin colour is the face's colour index, so face and skin always match).
    std::vector<uint8> facialHair;
    std::vector<std::pair<uint8, uint8>> faces, hairs;
    for (CharSectionsEntry const* section : sCharSectionsStore)
    {
        if (section->RaceID != race || section->SexID != gender)
            continue;
        switch (section->BaseSection)
        {
            case SECTION_TYPE_FACE: faces.emplace_back(section->VariationIndex, section->ColorIndex); break;
            case SECTION_TYPE_FACIAL_HAIR: facialHair.push_back(section->VariationIndex); break;
            case SECTION_TYPE_HAIR: hairs.emplace_back(section->VariationIndex, section->ColorIndex); break;
            default: break;
        }
    }
    if (faces.empty() || hairs.empty())
    {
        error = "no character looks for race " + std::to_string(race);
        return 0;
    }
    std::pair<uint8, uint8> const face = faces[urand(0, faces.size() - 1)];
    std::pair<uint8, uint8> const hair = hairs[urand(0, hairs.size() - 1)];
    bool const noFacialHair = race == RACE_TAUREN || race == RACE_DRAENEI ||
                              (gender == GENDER_FEMALE && race != RACE_NIGHTELF && race != RACE_UNDEAD_PLAYER);
    uint8 const beard = noFacialHair || facialHair.empty() ? 0 : facialHair[urand(0, facialHair.size() - 1)];

    auto session = std::make_unique<WorldSession>(account, "", 0x0, nullptr, SEC_PLAYER,
                                                  EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), LOCALE_enUS, 0, false,
                                                  false, 0);
    CharacterCreateInfo info(name, race, cls, gender, face.second, face.first, hair.first, hair.second, beard);
    uint32 const guid = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();
    Player* player = new Player(session.get());
    player->GetMotionMaster()->Initialize();
    if (!player->Create(guid, &info))
    {
        player->CleanupsBeforeDelete();
        delete player;
        error = "the game refused to create " + name;
        return 0;
    }
    player->setCinematic(2);
    player->SetAtLoginFlag(AT_LOGIN_NONE);
    player->SaveToDB(true, false);
    sCharacterCache->AddCharacterCacheEntry(player->GetGUID(), account, player->GetName(), player->getGender(),
                                            player->getRace(), player->getClass(), player->GetLevel());
    player->CleanupsBeforeDelete();
    delete player;
    return guid;
}
