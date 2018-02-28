/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2002 EQEMu Development Team (http://eqemu.org)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "../common/bodytypes.h"
#include "../common/classes.h"
#include "../common/global_define.h"
#include "../common/misc_functions.h"
#include "../common/rulesys.h"
#include "../common/seperator.h"
#include "../common/spdat.h"
#include "../common/string_util.h"
#include "../common/emu_versions.h"
#include "../common/features.h"
#include "../common/item_instance.h"
#include "../common/item_data.h"
#include "../common/linked_list.h"
#include "../common/servertalk.h"
#include "../common/say_link.h"

#include "client.h"
#include "entity.h"
#include "npc.h"
#include "string_ids.h"
#include "spawn2.h"
#include "zone.h"
#include "quest_parser_collection.h"

#include <cctype>
#include <stdio.h>
#include <string>

#ifdef _WINDOWS
#define snprintf	_snprintf
#define strncasecmp	_strnicmp
#define strcasecmp	_stricmp
#else
#include <stdlib.h>
#include <pthread.h>
#endif

extern Zone* zone;
extern volatile bool is_zone_loaded;
extern EntityList entity_list;

NPC::NPC(const NPCType* d, Spawn2* in_respawn, const glm::vec4& position, int iflymode, bool IsCorpse)
: Mob(d->name,
		d->lastname,
		d->max_hp,
		d->max_hp,
		d->gender,
		d->race,
		d->class_,
		(bodyType)d->bodytype,
		d->deity,
		d->level,
		d->npc_id,
		d->size,
		d->runspeed,
		position,
		d->light, // innate_light
		d->texture,
		d->helmtexture,
		d->AC,
		d->ATK,
		d->STR,
		d->STA,
		d->DEX,
		d->AGI,
		d->INT,
		d->WIS,
		d->CHA,
		d->haircolor,
		d->beardcolor,
		d->eyecolor1,
		d->eyecolor2,
		d->hairstyle,
		d->luclinface,
		d->beard,
		d->drakkin_heritage,
		d->drakkin_tattoo,
		d->drakkin_details,
		d->armor_tint,
		0,
		d->see_invis,			// pass see_invis/see_ivu flags to mob constructor
		d->see_invis_undead,
		d->see_hide,
		d->see_improved_hide,
		d->hp_regen,
		d->mana_regen,
		d->qglobal,
		d->maxlevel,
		d->scalerate,
		d->armtexture,
		d->bracertexture,
		d->handtexture,
		d->legtexture,
		d->feettexture),
	attacked_timer(CombatEventTimer_expire),
	swarm_timer(100),
	classattack_timer(1000),
	knightattack_timer(1000),
	assist_timer(AIassistcheck_delay),
	qglobal_purge_timer(30000),
	sendhpupdate_timer(2000),
	enraged_timer(1000),
	taunt_timer(TauntReuseTime * 1000),
	m_SpawnPoint(position),
	m_GuardPoint(-1,-1,-1,0),
	m_GuardPointSaved(0,0,0,0)
{
	//What is the point of this, since the names get mangled..
	Mob* mob = entity_list.GetMob(name);
	if(mob != 0)
		entity_list.RemoveEntity(mob->GetID());

	int moblevel=GetLevel();

	NPCTypedata = d;
	NPCTypedata_ours = nullptr;
	respawn2 = in_respawn;
	swarm_timer.Disable();

	taunting = false;
	proximity = nullptr;
	copper = 0;
	silver = 0;
	gold = 0;
	platinum = 0;
	max_dmg = d->max_dmg;
	min_dmg = d->min_dmg;
	attack_count = d->attack_count;
	grid = 0;
	wp_m = 0;
	max_wp=0;
	save_wp = 0;
	spawn_group = 0;
	swarmInfoPtr = nullptr;
	spellscale = d->spellscale;
	healscale = d->healscale;

	logging_enabled = NPC_DEFAULT_LOGGING_ENABLED;

	pAggroRange = d->aggroradius;
	pAssistRange = d->assistradius;
	findable = d->findable;
	trackable = d->trackable;

	MR = d->MR;
	CR = d->CR;
	DR = d->DR;
	FR = d->FR;
	PR = d->PR;
	Corrup = d->Corrup;
	PhR = d->PhR;

	STR = d->STR;
	STA = d->STA;
	AGI = d->AGI;
	DEX = d->DEX;
	INT = d->INT;
	WIS = d->WIS;
	CHA = d->CHA;
	npc_mana = d->Mana;

	//quick fix of ordering if they screwed it up in the DB
	if(max_dmg < min_dmg) {
		int tmp = min_dmg;
		min_dmg = max_dmg;
		max_dmg = tmp;
	}

	// Max Level and Stat Scaling if maxlevel is set
	if(maxlevel > level)
	{
		LevelScale();
	}

	// Set Resists if they are 0 in the DB
	CalcNPCResists();

	// Set Mana and HP Regen Rates if they are 0 in the DB
	CalcNPCRegen();

	// Set Min and Max Damage if they are 0 in the DB
	if(max_dmg == 0){
		CalcNPCDamage();
	}

	base_damage = round((max_dmg - min_dmg) / 1.9);
	min_damage = min_dmg - round(base_damage / 10.0);

	accuracy_rating = d->accuracy_rating;
	avoidance_rating = d->avoidance_rating;
	ATK = d->ATK;

	// used for when switch back to charm
	default_ac = d->AC;
	default_min_dmg = min_dmg;
	default_max_dmg = max_dmg;
	default_attack_delay = d->attack_delay;
	default_accuracy_rating = d->accuracy_rating;
	default_avoidance_rating = d->avoidance_rating;
	default_atk = d->ATK;

	// used for when getting charmed, if 0, doesn't swap
	charm_ac = d->charm_ac;
	charm_min_dmg = d->charm_min_dmg;
	charm_max_dmg = d->charm_max_dmg;
	charm_attack_delay = d->charm_attack_delay;
	charm_accuracy_rating = d->charm_accuracy_rating;
	charm_avoidance_rating = d->charm_avoidance_rating;
	charm_atk = d->charm_atk;

	CalcMaxMana();
	SetMana(GetMaxMana());

	MerchantType = d->merchanttype;
	merchant_open = GetClass() == MERCHANT;
	adventure_template_id = d->adventure_template;
	flymode = iflymode;
	guard_anim = eaStanding;
	roambox_distance = 0;
	roambox_max_x = -2;
	roambox_max_y = -2;
	roambox_min_x = -2;
	roambox_min_y = -2;
	roambox_movingto_x = -2;
	roambox_movingto_y = -2;
	roambox_min_delay = 1000;
	roambox_delay = 1000;
	p_depop = false;
	loottable_id = d->loottable_id;
	skip_global_loot = d->skip_global_loot;
	rare_spawn = d->rare_spawn;

	no_target_hotkey = d->no_target_hotkey;

	primary_faction = 0;
	SetNPCFactionID(d->npc_faction_id);

	npc_spells_id = 0;
	HasAISpell = false;
	HasAISpellEffects = false;
	innate_proc_spell_id = 0;

	if(GetClass() == MERCERNARY_MASTER && RuleB(Mercs, AllowMercs))
	{
		LoadMercTypes();
		LoadMercs();
	}

	SpellFocusDMG = 0;
	SpellFocusHeal = 0;

	pet_spell_id = 0;

	delaytimer = false;
	combat_event = false;
	attack_speed = d->attack_speed;
	attack_delay = d->attack_delay;
	slow_mitigation = d->slow_mitigation;

	EntityList::RemoveNumbers(name);
	entity_list.MakeNameUnique(name);

	npc_aggro = d->npc_aggro;

	AI_Init();
	AI_Start();

	d_melee_texture1 = d->d_melee_texture1;
	d_melee_texture2 = d->d_melee_texture2;
	herosforgemodel = d->herosforgemodel;

	ammo_idfile = d->ammo_idfile;
	memset(equipment, 0, sizeof(equipment));
	prim_melee_type = d->prim_melee_type;
	sec_melee_type = d->sec_melee_type;
	ranged_type = d->ranged_type;

	// If Melee Textures are not set, set attack type to Hand to Hand as default
	if(!d_melee_texture1)
		prim_melee_type = 28;
	if(!d_melee_texture2)
		sec_melee_type = 28;

	//give NPCs skill values...
	int r;
	for (r = 0; r <= EQEmu::skills::HIGHEST_SKILL; r++) {
		skills[r] = database.GetSkillCap(GetClass(), (EQEmu::skills::SkillType)r, moblevel);
	}
	// some overrides -- really we need to be able to set skills for mobs in the DB
	// There are some known low level SHM/BST pets that do not follow this, which supports
	// the theory of needing to be able to set skills for each mob separately
	if (moblevel > 50) {
		skills[EQEmu::skills::SkillDoubleAttack] = 250;
		skills[EQEmu::skills::SkillDualWield] = 250;
	} else if (moblevel > 3) {
		skills[EQEmu::skills::SkillDoubleAttack] = moblevel * 5;
		skills[EQEmu::skills::SkillDualWield] = skills[EQEmu::skills::SkillDoubleAttack];
	} else {
		skills[EQEmu::skills::SkillDoubleAttack] = moblevel * 5;
	}

	if(d->trap_template > 0)
	{
		std::map<uint32,std::list<LDoNTrapTemplate*> >::iterator trap_ent_iter;
		std::list<LDoNTrapTemplate*> trap_list;

		trap_ent_iter = zone->ldon_trap_entry_list.find(d->trap_template);
		if(trap_ent_iter != zone->ldon_trap_entry_list.end())
		{
			trap_list = trap_ent_iter->second;
			if(trap_list.size() > 0)
			{
				auto trap_list_iter = trap_list.begin();
				std::advance(trap_list_iter, zone->random.Int(0, trap_list.size() - 1));
				LDoNTrapTemplate* tt = (*trap_list_iter);
				if(tt)
				{
					if((uint8)tt->spell_id > 0)
					{
						ldon_trapped = true;
						ldon_spell_id = tt->spell_id;
					}
					else
					{
						ldon_trapped = false;
						ldon_spell_id = 0;
					}

					ldon_trap_type = (uint8)tt->type;
					if(tt->locked > 0)
					{
						ldon_locked = true;
						ldon_locked_skill = tt->skill;
					}
					else
					{
						ldon_locked = false;
						ldon_locked_skill = 0;
					}
					ldon_trap_detected = 0;
				}
			}
			else
			{
				ldon_trapped = false;
				ldon_trap_type = 0;
				ldon_spell_id = 0;
				ldon_locked = false;
				ldon_locked_skill = 0;
				ldon_trap_detected = 0;
			}
		}
		else
		{
			ldon_trapped = false;
			ldon_trap_type = 0;
			ldon_spell_id = 0;
			ldon_locked = false;
			ldon_locked_skill = 0;
			ldon_trap_detected = 0;
		}
	}
	else
	{
		ldon_trapped = false;
		ldon_trap_type = 0;
		ldon_spell_id = 0;
		ldon_locked = false;
		ldon_locked_skill = 0;
		ldon_trap_detected = 0;
	}

	reface_timer = new Timer(15000);
	reface_timer->Disable();
	qGlobals = nullptr;
	SetEmoteID(d->emoteid);
	InitializeBuffSlots();
	CalcBonuses();
	raid_target = d->raid_target;
	ignore_despawn = d->ignore_despawn;
	m_targetable = !d->untargetable;

	Hijack(d, in_respawn);
	CalcBonuses();
}

NPC::~NPC()
{
	AI_Stop();

	if(proximity != nullptr) {
		entity_list.RemoveProximity(GetID());
		safe_delete(proximity);
	}

	safe_delete(NPCTypedata_ours);

	{
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		safe_delete(item);
	}
	itemlist.clear();
	}

	{
	std::list<struct NPCFaction*>::iterator cur,end;
	cur = faction_list.begin();
	end = faction_list.end();
	for(; cur != end; ++cur) {
		struct NPCFaction* fac = *cur;
		safe_delete(fac);
	}
	faction_list.clear();
	}

	safe_delete(reface_timer);
	safe_delete(swarmInfoPtr);
	safe_delete(qGlobals);
	UninitializeBuffSlots();
}

void NPC::SetTarget(Mob* mob) {
	if(mob == GetTarget())		//dont bother if they are allready our target
		return;

	//This is not the default behavior for swarm pets, must be specified from quest functions or rules value.
	if(GetSwarmInfo() && GetSwarmInfo()->target && GetTarget() && (GetTarget()->GetHP() > 0)) {
		Mob *targ = entity_list.GetMob(GetSwarmInfo()->target);
		if(targ != mob){
			return;
		}
	}

	if (mob) {
		SetAttackTimer();
	} else {
		ranged_timer.Disable();
		//attack_timer.Disable();
		attack_dw_timer.Disable();
	}

	// either normal pet and owner is client or charmed pet and owner is client
	Mob *owner = nullptr;
	if (IsPet() && IsPetOwnerClient()) {
		owner = GetOwner();
	} else if (IsCharmed()) {
		owner = GetOwner();
		if (owner && !owner->IsClient())
			owner = nullptr;
	}

	if (owner) {
		auto client = owner->CastToClient();
		if (client->ClientVersionBit() & EQEmu::versions::bit_UFAndLater) {
			auto app = new EQApplicationPacket(OP_PetHoTT, sizeof(ClientTarget_Struct));
			auto ct = (ClientTarget_Struct *)app->pBuffer;
			ct->new_target = mob ? mob->GetID() : 0;
			client->FastQueuePacket(&app);
		}
	}
	Mob::SetTarget(mob);
}

ServerLootItem_Struct* NPC::GetItem(int slot_id) {
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		if (item->equip_slot == slot_id) {
			return item;
		}
	}
	return(nullptr);
}

void NPC::RemoveItem(uint32 item_id, uint16 quantity, uint16 slot) {
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		if (item->item_id == item_id && slot <= 0 && quantity <= 0) {
			itemlist.erase(cur);
			UpdateEquipmentLight();
			if (UpdateActiveLight()) { SendAppearancePacket(AT_Light, GetActiveLightType()); }
			return;
		}
		else if (item->item_id == item_id && item->equip_slot == slot && quantity >= 1) {
			if (item->charges <= quantity) {
				itemlist.erase(cur);
				UpdateEquipmentLight();
				if (UpdateActiveLight()) { SendAppearancePacket(AT_Light, GetActiveLightType()); }
			}
			else {
				item->charges -= quantity;
			}
			return;
		}
	}
}

void NPC::CheckMinMaxLevel(Mob *them)
{
	if(them == nullptr || !them->IsClient())
		return;

	uint16 themlevel = them->GetLevel();
	uint8 material;

	auto cur = itemlist.begin();
	while(cur != itemlist.end())
	{
		if(!(*cur))
			return;

		if(themlevel < (*cur)->min_level || themlevel > (*cur)->max_level)
		{
			material = EQEmu::InventoryProfile::CalcMaterialFromSlot((*cur)->equip_slot);
			if (material != EQEmu::textures::materialInvalid)
				SendWearChange(material);

			cur = itemlist.erase(cur);
			continue;
		}
		++cur;
	}

	UpdateEquipmentLight();
	if (UpdateActiveLight())
		SendAppearancePacket(AT_Light, GetActiveLightType());
}

void NPC::ClearItemList() {
	ItemList::iterator cur,end;
	cur = itemlist.begin();
	end = itemlist.end();
	for(; cur != end; ++cur) {
		ServerLootItem_Struct* item = *cur;
		safe_delete(item);
	}
	itemlist.clear();

	UpdateEquipmentLight();
	if (UpdateActiveLight())
		SendAppearancePacket(AT_Light, GetActiveLightType());
}

void NPC::QueryLoot(Client* to)
{
	to->Message(0, "Coin: %ip %ig %is %ic", platinum, gold, silver, copper);

	int x = 0;
	for (auto cur = itemlist.begin(); cur != itemlist.end(); ++cur, ++x) {
		if (!(*cur)) {
			Log(Logs::General, Logs::Error, "NPC::QueryLoot() - ItemList error, null item");
			continue;
		}
		if (!(*cur)->item_id || !database.GetItem((*cur)->item_id)) {
			Log(Logs::General, Logs::Error, "NPC::QueryLoot() - Database error, invalid item");
			continue;
		}

		EQEmu::SayLinkEngine linker;
		linker.SetLinkType(EQEmu::saylink::SayLinkLootItem);
		linker.SetLootData(*cur);

		auto item_link = linker.GenerateLink();

		to->Message(0, "%s, ID: %u, Level: (min: %u, max: %u)", item_link.c_str(), (*cur)->item_id, (*cur)->min_level, (*cur)->max_level);
	}

	to->Message(0, "%i items on %s.", x, GetName());
}

void NPC::AddCash(uint16 in_copper, uint16 in_silver, uint16 in_gold, uint16 in_platinum) {
	if(in_copper >= 0)
		copper = in_copper;
	else
		copper = 0;

	if(in_silver >= 0)
		silver = in_silver;
	else
		silver = 0;

	if(in_gold >= 0)
		gold = in_gold;
	else
		gold = 0;

	if(in_platinum >= 0)
		platinum = in_platinum;
	else
		platinum = 0;
}

void NPC::AddCash() {
	copper = zone->random.Int(1, 100);
	silver = zone->random.Int(1, 50);
	gold = zone->random.Int(1, 10);
	platinum = zone->random.Int(1, 5);
}

void NPC::RemoveCash() {
	copper = 0;
	silver = 0;
	gold = 0;
	platinum = 0;
}

bool NPC::Process()
{
	if (IsStunned() && stunned_timer.Check()) {
		Mob::UnStun();
		this->spun_timer.Disable();
	}

	if (p_depop)
	{
		Mob* owner = entity_list.GetMob(this->ownerid);
		if (owner != 0)
		{
			//if(GetBodyType() != BT_SwarmPet)
			// owner->SetPetID(0);
			this->ownerid = 0;
			this->petid = 0;
		}
		return false;
	}

	SpellProcess();

	if (tic_timer.Check()) {
		parse->EventNPC(EVENT_TICK, this, nullptr, "", 0);
		BuffProcess();

		if (currently_fleeing)
			ProcessFlee();

		uint32 sitting_bonus = 0;
		uint32 petbonus = 0;
		uint32 bestregen = 0;
		int32 dbregen = GetNPCHPRegen();

		if (GetAppearance() == eaSitting)
			sitting_bonus += 3;

		int32 OOCRegen = 0;
		if (oocregen > 0) { //should pull from Mob class
			OOCRegen += GetMaxHP() * oocregen / 100;
		}

		// Fixing NPC regen.NPCs should regen to full during 
		// a set duration, not based on their HPs.Increase NPC's HPs by 
		// % of total HPs / tick.
		//
		// If oocregen set in db, apply to pets as well.
		// This allows the obscene #s for pets in the db to be tweaked
		// while maintaining a decent ooc regen.

		bestregen = std::max(dbregen,OOCRegen);

		if ((GetHP() < GetMaxHP()) && !IsPet()) {
			if (!IsEngaged())
				SetHP(GetHP() + bestregen + sitting_bonus);
			else
				SetHP(GetHP() + dbregen);
		}
		else if (GetHP() < GetMaxHP() && GetOwnerID() != 0) {
			if (!IsEngaged()) {
				if (oocregen > 0) {
					petbonus = std::max(OOCRegen,dbregen);
				}
				else {
					petbonus = dbregen + (GetLevel() / 5);
				}

				SetHP(GetHP() + sitting_bonus + petbonus);
			}
			else
				SetHP(GetHP() + dbregen);
		}
		else
			SetHP(GetHP() + dbregen + sitting_bonus);

		if (GetMana() < GetMaxMana()) {
			SetMana(GetMana() + mana_regen + sitting_bonus);
		}


		if (zone->adv_data && !p_depop)
		{
			ServerZoneAdventureDataReply_Struct* ds = (ServerZoneAdventureDataReply_Struct*)zone->adv_data;
			if (ds->type == Adventure_Rescue && ds->data_id == GetNPCTypeID())
			{
				Mob *o = GetOwner();
				if (o && o->IsClient())
				{
					float x_diff = ds->dest_x - GetX();
					float y_diff = ds->dest_y - GetY();
					float z_diff = ds->dest_z - GetZ();
					float dist = ((x_diff * x_diff) + (y_diff * y_diff) + (z_diff * z_diff));
					if (dist < RuleR(Adventure, DistanceForRescueComplete))
					{
						zone->DoAdventureCountIncrease();
						Say("You don't know what this means to me. Thank you so much for finding and saving me from"
							" this wretched place. I'll find my way from here.");
						Depop();
					}
				}
			}
		}
	}

	// we might actually want to reset in this check ... won't until issues arise at least :P
	if (sendhpupdate_timer.Check(false) && (IsTargeted() || (IsPet() && GetOwner() && GetOwner()->IsClient()))) {
		if(!IsFullHP || cur_hp<max_hp){
			SendHPUpdate();
		}
	}

	if(HasVirus()) {
		if(viral_timer.Check()) {
			viral_timer_counter++;
			for(int i = 0; i < MAX_SPELL_TRIGGER*2; i+=2) {
				if(viral_spells[i] && spells[viral_spells[i]].viral_timer > 0)	{
					if(viral_timer_counter % spells[viral_spells[i]].viral_timer == 0) {
						SpreadVirus(viral_spells[i], viral_spells[i+1]);
					}
				}
			}
		}
		if(viral_timer_counter > 999)
			viral_timer_counter = 0;
	}

	if(spellbonuses.GravityEffect == 1) {
		if(gravity_timer.Check())
			DoGravityEffect();
	}

	if(reface_timer->Check() && !IsEngaged() && (m_GuardPoint.x == GetX() && m_GuardPoint.y == GetY() && m_GuardPoint.z == GetZ())) {
		SetHeading(m_GuardPoint.w);
		SendPosition();
		reface_timer->Disable();
	}

	if (IsMezzed())
		return true;

	if(IsStunned()) {
		if(spun_timer.Check())
			Spin();
		return true;
	}

	if (enraged_timer.Check()){
		ProcessEnrage();

		/* Don't keep running the check every second if we don't have enrage */
		if (!GetSpecialAbility(SPECATK_ENRAGE)) {
			enraged_timer.Disable();
		}
	}

	//Handle assists...
	if (assist_cap_timer.Check()) {
		if (NPCAssistCap() > 0)
			DelAssistCap();
		else
			assist_cap_timer.Disable();
	}

	if (assist_timer.Check() && IsEngaged() && !Charmed() && !HasAssistAggro() &&
	    NPCAssistCap() < RuleI(Combat, NPCAssistCap)) {
		entity_list.AIYellForHelp(this, GetTarget());
		if (NPCAssistCap() > 0 && !assist_cap_timer.Enabled())
			assist_cap_timer.Start(RuleI(Combat, NPCAssistCapTimer));
	}

	if(qGlobals)
	{
		if(qglobal_purge_timer.Check())
		{
			qGlobals->PurgeExpiredGlobals();
		}
	}

	AI_Process();

	return true;
}

uint32 NPC::CountLoot() {
	return(itemlist.size());
}

void NPC::UpdateEquipmentLight()
{
	m_Light.Type[EQEmu::lightsource::LightEquipment] = 0;
	m_Light.Level[EQEmu::lightsource::LightEquipment] = 0;

	for (int index = EQEmu::inventory::slotBegin; index < EQEmu::legacy::EQUIPMENT_SIZE; ++index) {
		if (index == EQEmu::inventory::slotAmmo) { continue; }

		auto item = database.GetItem(equipment[index]);
		if (item == nullptr) { continue; }

		if (EQEmu::lightsource::IsLevelGreater(item->Light, m_Light.Type[EQEmu::lightsource::LightEquipment])) {
			m_Light.Type[EQEmu::lightsource::LightEquipment] = item->Light;
			m_Light.Level[EQEmu::lightsource::LightEquipment] = EQEmu::lightsource::TypeToLevel(m_Light.Type[EQEmu::lightsource::LightEquipment]);
		}
	}

	uint8 general_light_type = 0;
	for (auto iter = itemlist.begin(); iter != itemlist.end(); ++iter) {
		auto item = database.GetItem((*iter)->item_id);
		if (item == nullptr) { continue; }

		if (!item->IsClassCommon()) { continue; }
		if (item->Light < 9 || item->Light > 13) { continue; }

		if (EQEmu::lightsource::TypeToLevel(item->Light))
			general_light_type = item->Light;
	}

	if (EQEmu::lightsource::IsLevelGreater(general_light_type, m_Light.Type[EQEmu::lightsource::LightEquipment]))
		m_Light.Type[EQEmu::lightsource::LightEquipment] = general_light_type;

	m_Light.Level[EQEmu::lightsource::LightEquipment] = EQEmu::lightsource::TypeToLevel(m_Light.Type[EQEmu::lightsource::LightEquipment]);
}

void NPC::Depop(bool StartSpawnTimer) {
	uint16 emoteid = this->GetEmoteID();
	if(emoteid != 0)
		this->DoNPCEmote(ONDESPAWN,emoteid);
	p_depop = true;
	if (StartSpawnTimer) {
		if (respawn2 != 0) {
			respawn2->DeathReset();
		}
	}
}

bool NPC::DatabaseCastAccepted(int spell_id) {
	for (int i=0; i < 12; i++) {
		switch(spells[spell_id].effectid[i]) {
		case SE_Stamina: {
			if(IsEngaged() && GetHPRatio() < 100)
				return true;
			else
				return false;
			break;
		}
		case SE_CurrentHPOnce:
		case SE_CurrentHP: {
			if(this->GetHPRatio() < 100 && spells[spell_id].buffduration == 0)
				return true;
			else
				return false;
			break;
		}

		case SE_HealOverTime: {
			if(this->GetHPRatio() < 100)
				return true;
			else
				return false;
			break;
		}
		case SE_DamageShield: {
			return true;
		}
		case SE_NecPet:
		case SE_SummonPet: {
			if(GetPet()){
#ifdef SPELLQUEUE
				printf("%s: Attempted to make a second pet, denied.\n",GetName());
#endif
				return false;
			}
			break;
		}
		case SE_LocateCorpse:
		case SE_SummonCorpse: {
			return false; //Pfft, npcs don't need to summon corpses/locate corpses!
			break;
		}
		default:
			if(spells[spell_id].goodEffect == 1 && !(spells[spell_id].buffduration == 0 && this->GetHPRatio() == 100) && !IsEngaged())
				return true;
			return false;
		}
	}
	return false;
}

bool NPC::SpawnZoneController(){

	if (!RuleB(Zone, UseZoneController))
		return false;

	auto npc_type = new NPCType;
	memset(npc_type, 0, sizeof(NPCType));

	strncpy(npc_type->name, "zone_controller", 60);
	npc_type->cur_hp = 2000000000;
	npc_type->max_hp = 2000000000;
	npc_type->hp_regen = 100000000;
	npc_type->race = 240;
	npc_type->size = .1;
	npc_type->gender = 2;
	npc_type->class_ = 1;
	npc_type->deity = 1;
	npc_type->level = 200;
	npc_type->npc_id = ZONE_CONTROLLER_NPC_ID;
	npc_type->loottable_id = 0;
	npc_type->texture = 3;
	npc_type->runspeed = 0;
	npc_type->d_melee_texture1 = 0;
	npc_type->d_melee_texture2 = 0;
	npc_type->merchanttype = 0;
	npc_type->bodytype = 11;
	npc_type->skip_global_loot = true;

	if (RuleB(Zone, EnableZoneControllerGlobals)) {
		npc_type->qglobal = true;
	}

	npc_type->prim_melee_type = 28;
	npc_type->sec_melee_type = 28;

	npc_type->findable = 0;
	npc_type->trackable = 0;

	strcpy(npc_type->special_abilities, "12,1^13,1^14,1^15,1^16,1^17,1^19,1^22,1^24,1^25,1^28,1^31,1^35,1^39,1^42,1");

	glm::vec4 point;
	point.x = 3000;
	point.y = 1000;
	point.z = 500;

	auto npc = new NPC(npc_type, nullptr, point, FlyMode3);
	npc->GiveNPCTypeData(npc_type);

	entity_list.AddNPC(npc);

	return true;
}

NPC* NPC::SpawnNPC(const char* spawncommand, const glm::vec4& position, Client* client) {
	if(spawncommand == 0 || spawncommand[0] == 0) {
		return 0;
	}
	else {
		Seperator sep(spawncommand);
		//Lets see if someone didn't fill out the whole #spawn function properly
		if (!sep.IsNumber(1))
			sprintf(sep.arg[1],"1");
		if (!sep.IsNumber(2))
			sprintf(sep.arg[2],"1");
		if (!sep.IsNumber(3))
			sprintf(sep.arg[3],"0");
		if (atoi(sep.arg[4]) > 2100000000 || atoi(sep.arg[4]) <= 0)
			sprintf(sep.arg[4]," ");
		if (!strcmp(sep.arg[5],"-"))
			sprintf(sep.arg[5]," ");
		if (!sep.IsNumber(5))
			sprintf(sep.arg[5]," ");
		if (!sep.IsNumber(6))
			sprintf(sep.arg[6],"1");
		if (!sep.IsNumber(8))
			sprintf(sep.arg[8],"0");
		if (!sep.IsNumber(9))
			sprintf(sep.arg[9], "0");
		if (!sep.IsNumber(7))
			sprintf(sep.arg[7],"0");
		if (!strcmp(sep.arg[4],"-"))
			sprintf(sep.arg[4]," ");
		if (!sep.IsNumber(10))	// bodytype
			sprintf(sep.arg[10], "0");
		//Calc MaxHP if client neglected to enter it...
		if (!sep.IsNumber(4)) {
			//Stolen from Client::GetMaxHP...
			uint8 multiplier = 0;
			int tmplevel = atoi(sep.arg[2]);
			switch(atoi(sep.arg[5]))
			{
			case WARRIOR:
				if (tmplevel < 20)
					multiplier = 22;
				else if (tmplevel < 30)
					multiplier = 23;
				else if (tmplevel < 40)
					multiplier = 25;
				else if (tmplevel < 53)
					multiplier = 27;
				else if (tmplevel < 57)
					multiplier = 28;
				else
					multiplier = 30;
				break;

			case DRUID:
			case CLERIC:
			case SHAMAN:
				multiplier = 15;
				break;

			case PALADIN:
			case SHADOWKNIGHT:
				if (tmplevel < 35)
					multiplier = 21;
				else if (tmplevel < 45)
					multiplier = 22;
				else if (tmplevel < 51)
					multiplier = 23;
				else if (tmplevel < 56)
					multiplier = 24;
				else if (tmplevel < 60)
					multiplier = 25;
				else
					multiplier = 26;
				break;

			case MONK:
			case BARD:
			case ROGUE:
			//case BEASTLORD:
				if (tmplevel < 51)
					multiplier = 18;
				else if (tmplevel < 58)
					multiplier = 19;
				else
					multiplier = 20;
				break;

			case RANGER:
				if (tmplevel < 58)
					multiplier = 20;
				else
					multiplier = 21;
				break;

			case MAGICIAN:
			case WIZARD:
			case NECROMANCER:
			case ENCHANTER:
				multiplier = 12;
				break;

			default:
				if (tmplevel < 35)
					multiplier = 21;
				else if (tmplevel < 45)
					multiplier = 22;
				else if (tmplevel < 51)
					multiplier = 23;
				else if (tmplevel < 56)
					multiplier = 24;
				else if (tmplevel < 60)
					multiplier = 25;
				else
					multiplier = 26;
				break;
			}
			sprintf(sep.arg[4],"%i",5+multiplier*atoi(sep.arg[2])+multiplier*atoi(sep.arg[2])*75/300);
		}

		// Autoselect NPC Gender
		if (sep.arg[5][0] == 0) {
			sprintf(sep.arg[5], "%i", (int) Mob::GetDefaultGender(atoi(sep.arg[1])));
		}

		//Time to create the NPC!!
		auto npc_type = new NPCType;
		memset(npc_type, 0, sizeof(NPCType));

		strncpy(npc_type->name, sep.arg[0], 60);
		npc_type->cur_hp = atoi(sep.arg[4]);
		npc_type->max_hp = atoi(sep.arg[4]);
		npc_type->race = atoi(sep.arg[1]);
		npc_type->gender = atoi(sep.arg[5]);
		npc_type->class_ = atoi(sep.arg[6]);
		npc_type->deity = 1;
		npc_type->level = atoi(sep.arg[2]);
		npc_type->npc_id = 0;
		npc_type->loottable_id = 0;
		npc_type->texture = atoi(sep.arg[3]);
		npc_type->light = 0; // spawncommand needs update
		npc_type->runspeed = 1.25;
		npc_type->d_melee_texture1 = atoi(sep.arg[7]);
		npc_type->d_melee_texture2 = atoi(sep.arg[8]);
		npc_type->merchanttype = atoi(sep.arg[9]);
		npc_type->bodytype = atoi(sep.arg[10]);

		npc_type->STR = 150;
		npc_type->STA = 150;
		npc_type->DEX = 150;
		npc_type->AGI = 150;
		npc_type->INT = 150;
		npc_type->WIS = 150;
		npc_type->CHA = 150;

		npc_type->attack_delay = 3000;

		npc_type->prim_melee_type = 28;
		npc_type->sec_melee_type = 28;

		auto npc = new NPC(npc_type, nullptr, position, FlyMode3);
		npc->GiveNPCTypeData(npc_type);

		entity_list.AddNPC(npc);

		if (client) {
			// Notify client of spawn data
			client->Message(0, "New spawn:");
			client->Message(0, "Name: %s", npc->name);
			client->Message(0, "Race: %u", npc->race);
			client->Message(0, "Level: %u", npc->level);
			client->Message(0, "Material: %u", npc->texture);
			client->Message(0, "Current/Max HP: %i", npc->max_hp);
			client->Message(0, "Gender: %u", npc->gender);
			client->Message(0, "Class: %u", npc->class_);
			client->Message(0, "Weapon Item Number: %u/%u", npc->d_melee_texture1, npc->d_melee_texture2);
			client->Message(0, "MerchantID: %u", npc->MerchantType);
			client->Message(0, "Bodytype: %u", npc->bodytype);
		}

		return npc;
	}
}

uint32 ZoneDatabase::CreateNewNPCCommand(const char *zone, uint32 zone_version, Client *client, NPC *spawn,
					 uint32 extra)
{
	uint32 npc_type_id = 0;

	if (extra && client && client->GetZoneID()) {
		// Set an npc_type ID within the standard range for the current zone if possible (zone_id * 1000)
		int starting_npc_id = client->GetZoneID() * 1000;

		std::string query = StringFormat("SELECT MAX(id) FROM npc_types WHERE id >= %i AND id < %i",
						 starting_npc_id, starting_npc_id + 1000);
		auto results = QueryDatabase(query);
		if (results.Success()) {
			if (results.RowCount() != 0) {
				auto row = results.begin();
				npc_type_id = atoi(row[0]) + 1;
				// Prevent the npc_type id from exceeding the range for this zone
				if (npc_type_id >= (starting_npc_id + 1000))
					npc_type_id = 0;
			} else // No npc_type IDs set in this range yet
				npc_type_id = starting_npc_id;
		}
	}

	char tmpstr[64];
	EntityList::RemoveNumbers(strn0cpy(tmpstr, spawn->GetName(), sizeof(tmpstr)));
	std::string query;
	if (npc_type_id) {
		query = StringFormat("INSERT INTO npc_types (id, name, level, race, class, hp, gender, "
				     "texture, helmtexture, size, loottable_id, merchant_id, face, "
				     "runspeed, prim_melee_type, sec_melee_type) "
					 "VALUES(%i, \"%s\" , %i, %i, %i, %i, %i, %i, %i, %f, %i, %i, %i, %i, %i, %i)",
				     npc_type_id, tmpstr, spawn->GetLevel(), spawn->GetRace(), spawn->GetClass(),
				     spawn->GetMaxHP(), spawn->GetGender(), spawn->GetTexture(),
				     spawn->GetHelmTexture(), spawn->GetSize(), spawn->GetLoottableID(),
				     spawn->MerchantType, 0, spawn->GetRunspeed(), 28, 28);
		auto results = QueryDatabase(query);
		if (!results.Success()) {
			return false;
		}
		npc_type_id = results.LastInsertedID();
	} else {
		query = StringFormat("INSERT INTO npc_types (name, level, race, class, hp, gender, "
				     "texture, helmtexture, size, loottable_id, merchant_id, face, "
				     "runspeed, prim_melee_type, sec_melee_type) "
					 "VALUES(\"%s\", %i, %i, %i, %i, %i, %i, %i, %f, %i, %i, %i, %i, %i, %i)",
				     tmpstr, spawn->GetLevel(), spawn->GetRace(), spawn->GetClass(), spawn->GetMaxHP(),
				     spawn->GetGender(), spawn->GetTexture(), spawn->GetHelmTexture(), spawn->GetSize(),
				     spawn->GetLoottableID(), spawn->MerchantType, 0, spawn->GetRunspeed(), 28, 28);
		auto results = QueryDatabase(query);
		if (!results.Success()) {
			return false;
		}
		npc_type_id = results.LastInsertedID();
	}

	query = StringFormat("INSERT INTO spawngroup (id, name) VALUES(%i, '%s-%s')", 0, zone, spawn->GetName());
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		return false;
	}
	uint32 spawngroupid = results.LastInsertedID();

	spawn->SetSp2(spawngroupid);
	spawn->SetNPCTypeID(npc_type_id);

	query = StringFormat("INSERT INTO spawn2 (zone, version, x, y, z, respawntime, heading, spawngroupID) "
			     "VALUES('%s', %u, %f, %f, %f, %i, %f, %i)",
			     zone, zone_version, spawn->GetX(), spawn->GetY(), spawn->GetZ(), 1200, spawn->GetHeading(),
			     spawngroupid);
	results = QueryDatabase(query);
	if (!results.Success()) {
		return false;
	}

	query = StringFormat("INSERT INTO spawnentry (spawngroupID, npcID, chance) VALUES(%i, %i, %i)", spawngroupid,
			     npc_type_id, 100);
	results = QueryDatabase(query);
	if (!results.Success()) {
		return false;
	}

	return true;
}

uint32 ZoneDatabase::AddNewNPCSpawnGroupCommand(const char *zone, uint32 zone_version, Client *client, NPC *spawn,
						uint32 respawnTime)
{
	uint32 last_insert_id = 0;

	std::string query = StringFormat("INSERT INTO spawngroup (name) VALUES('%s%s%i')", zone, spawn->GetName(),
					 Timer::GetCurrentTime());
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		return 0;
	}
	last_insert_id = results.LastInsertedID();

	uint32 respawntime = 0;
	uint32 spawnid = 0;
	if (respawnTime)
		respawntime = respawnTime;
	else if (spawn->respawn2 && spawn->respawn2->RespawnTimer() != 0)
		respawntime = spawn->respawn2->RespawnTimer();
	else
		respawntime = 1200;

	query = StringFormat("INSERT INTO spawn2 (zone, version, x, y, z, respawntime, heading, spawngroupID) "
			     "VALUES('%s', %u, %f, %f, %f, %i, %f, %i)",
			     zone, zone_version, spawn->GetX(), spawn->GetY(), spawn->GetZ(), respawntime,
			     spawn->GetHeading(), last_insert_id);
	results = QueryDatabase(query);
	if (!results.Success()) {
		return 0;
	}
	spawnid = results.LastInsertedID();

	query = StringFormat("INSERT INTO spawnentry (spawngroupID, npcID, chance) VALUES(%i, %i, %i)", last_insert_id,
			     spawn->GetNPCTypeID(), 100);
	results = QueryDatabase(query);
	if (!results.Success()) {
		return 0;
	}

	return spawnid;
}

uint32 ZoneDatabase::UpdateNPCTypeAppearance(Client *client, NPC *spawn)
{
	std::string query =
	    StringFormat("UPDATE npc_types SET name = '%s', level = '%i', race = '%i', class = '%i', "
			 "hp = '%i', gender = '%i', texture = '%i', helmtexture = '%i', size = '%i', "
			 "loottable_id = '%i', merchant_id = '%i', face = '%i' "
			 "WHERE id = '%i'",
			 spawn->GetName(), spawn->GetLevel(), spawn->GetRace(), spawn->GetClass(), spawn->GetMaxHP(),
			 spawn->GetGender(), spawn->GetTexture(), spawn->GetHelmTexture(), spawn->GetSize(),
			 spawn->GetLoottableID(), spawn->MerchantType, spawn->GetLuclinFace(), spawn->GetNPCTypeID());
	auto results = QueryDatabase(query);
	return results.Success() == true ? 1 : 0;
}

uint32 ZoneDatabase::DeleteSpawnLeaveInNPCTypeTable(const char *zone, Client *client, NPC *spawn)
{
	uint32 id = 0;
	uint32 spawngroupID = 0;

	std::string query = StringFormat("SELECT id, spawngroupID FROM spawn2 WHERE "
					 "zone='%s' AND spawngroupID=%i",
					 zone, spawn->GetSp2());
	auto results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();
	if (row[0])
		id = atoi(row[0]);

	if (row[1])
		spawngroupID = atoi(row[1]);

	query = StringFormat("DELETE FROM spawn2 WHERE id = '%i'", id);
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	query = StringFormat("DELETE FROM spawngroup WHERE id = '%i'", spawngroupID);
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	query = StringFormat("DELETE FROM spawnentry WHERE spawngroupID = '%i'", spawngroupID);
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	return 1;
}

uint32 ZoneDatabase::DeleteSpawnRemoveFromNPCTypeTable(const char *zone, uint32 zone_version, Client *client,
						       NPC *spawn)
{
	uint32 id = 0;
	uint32 spawngroupID = 0;

	std::string query = StringFormat("SELECT id, spawngroupID FROM spawn2 WHERE zone = '%s' "
					 "AND version = %u AND spawngroupID = %i",
					 zone, zone_version, spawn->GetSp2());
	auto results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	if (results.RowCount() == 0)
		return 0;

	auto row = results.begin();

	if (row[0])
		id = atoi(row[0]);

	if (row[1])
		spawngroupID = atoi(row[1]);

	query = StringFormat("DELETE FROM spawn2 WHERE id = '%i'", id);
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	query = StringFormat("DELETE FROM spawngroup WHERE id = '%i'", spawngroupID);
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	query = StringFormat("DELETE FROM spawnentry WHERE spawngroupID = '%i'", spawngroupID);
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	query = StringFormat("DELETE FROM npc_types WHERE id = '%i'", spawn->GetNPCTypeID());
	results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	return 1;
}

uint32 ZoneDatabase::AddSpawnFromSpawnGroup(const char *zone, uint32 zone_version, Client *client, NPC *spawn,
					    uint32 spawnGroupID)
{
	uint32 last_insert_id = 0;
	std::string query =
	    StringFormat("INSERT INTO spawn2 (zone, version, x, y, z, respawntime, heading, spawngroupID) "
			 "VALUES('%s', %u, %f, %f, %f, %i, %f, %i)",
			 zone, zone_version, client->GetX(), client->GetY(), client->GetZ(), 120, client->GetHeading(),
			 spawnGroupID);
	auto results = QueryDatabase(query);
	if (!results.Success())
		return 0;

	return 1;
}

uint32 ZoneDatabase::AddNPCTypes(const char *zone, uint32 zone_version, Client *client, NPC *spawn, uint32 spawnGroupID)
{
	uint32 npc_type_id;
	char numberlessName[64];

	EntityList::RemoveNumbers(strn0cpy(numberlessName, spawn->GetName(), sizeof(numberlessName)));
	std::string query =
	    StringFormat("INSERT INTO npc_types (name, level, race, class, hp, gender, "
			 "texture, helmtexture, size, loottable_id, merchant_id, face, "
			 "runspeed, prim_melee_type, sec_melee_type) "
			 "VALUES(\"%s\", %i, %i, %i, %i, %i, %i, %i, %f, %i, %i, %i, %f, %i, %i)",
			 numberlessName, spawn->GetLevel(), spawn->GetRace(), spawn->GetClass(), spawn->GetMaxHP(),
			 spawn->GetGender(), spawn->GetTexture(), spawn->GetHelmTexture(), spawn->GetSize(),
			 spawn->GetLoottableID(), spawn->MerchantType, 0, spawn->GetRunspeed(), 28, 28);
	auto results = QueryDatabase(query);
	if (!results.Success())
		return 0;
	npc_type_id = results.LastInsertedID();

	if (client)
		client->Message(0, "%s npc_type ID %i created successfully!", numberlessName, npc_type_id);

	return 1;
}

uint32 ZoneDatabase::NPCSpawnDB(uint8 command, const char* zone, uint32 zone_version, Client *c, NPC* spawn, uint32 extra) {

	switch (command) {
		case 0: { // Create a new NPC and add all spawn related data
			return CreateNewNPCCommand(zone, zone_version, c, spawn, extra);
		}
		case 1:{ // Add new spawn group and spawn point for an existing NPC Type ID
			return AddNewNPCSpawnGroupCommand(zone, zone_version, c, spawn, extra);
		}
		case 2: { // Update npc_type appearance and other data on targeted spawn
			return UpdateNPCTypeAppearance(c, spawn);
		}
		case 3: { // delete spawn from spawning, but leave in npc_types table
			return DeleteSpawnLeaveInNPCTypeTable(zone, c, spawn);
		}
		case 4: { //delete spawn from DB (including npc_type)
			return DeleteSpawnRemoveFromNPCTypeTable(zone, zone_version, c, spawn);
		}
		case 5: { // add a spawn from spawngroup
			return AddSpawnFromSpawnGroup(zone, zone_version, c, spawn, extra);
        }
		case 6: { // add npc_type
			return AddNPCTypes(zone, zone_version, c, spawn, extra);
		}
	}
	return false;
}

int32 NPC::GetEquipmentMaterial(uint8 material_slot) const
{
	if (material_slot >= EQEmu::textures::materialCount)
		return 0;

	int16 invslot = EQEmu::InventoryProfile::CalcSlotFromMaterial(material_slot);
	if (invslot == INVALID_INDEX)
		return 0;

	if (equipment[invslot] == 0)
	{
		switch(material_slot)
		{
		case EQEmu::textures::armorHead:
			return helmtexture;
		case EQEmu::textures::armorChest:
			return texture;
		case EQEmu::textures::armorArms:
			return armtexture;
		case EQEmu::textures::armorWrist:
			return bracertexture;
		case EQEmu::textures::armorHands:
			return handtexture;
		case EQEmu::textures::armorLegs:
			return legtexture;
		case EQEmu::textures::armorFeet:
			return feettexture;
		case EQEmu::textures::weaponPrimary:
			return d_melee_texture1;
		case EQEmu::textures::weaponSecondary:
			return d_melee_texture2;
		default:
			//they have nothing in the slot, and its not a special slot... they get nothing.
			return(0);
		}
	}

	//they have some loot item in this slot, pass it up to the default handler
	return (Mob::GetEquipmentMaterial(material_slot));
}

uint32 NPC::GetMaxDamage(uint8 tlevel)
{
	uint32 dmg = 0;
	if (tlevel < 40)
		dmg = tlevel*2+2;
	else if (tlevel < 50)
		dmg = level*25/10+2;
	else if (tlevel < 60)
		dmg = (tlevel*3+2)+((tlevel-50)*30);
	else
		dmg = (tlevel*3+2)+((tlevel-50)*35);
	return dmg;
}

void NPC::PickPocket(Client* thief)
{
	thief->CheckIncreaseSkill(EQEmu::skills::SkillPickPockets, nullptr, 5);

	//make sure were allowed to target them:
	int over_level = GetLevel();
	if(over_level > (thief->GetLevel() + THIEF_PICKPOCKET_OVER)) {
		thief->Message(13, "You are too inexperienced to pick pocket this target");
		thief->SendPickPocketResponse(this, 0, PickPocketFailed);
		//should we check aggro
		return;
	}

	if(zone->random.Roll(5)) {
		AddToHateList(thief, 50);
		Say("Stop thief!");
		thief->Message(13, "You are noticed trying to steal!");
		thief->SendPickPocketResponse(this, 0, PickPocketFailed);
		return;
	}

	int steal_skill = thief->GetSkill(EQEmu::skills::SkillPickPockets);
	int steal_chance = steal_skill * 100 / (5 * over_level + 5);

	// Determine whether to steal money or an item.
	uint32 money[6] = { 0, ((steal_skill >= 125) ? (GetPlatinum()) : (0)), ((steal_skill >= 60) ? (GetGold()) : (0)), GetSilver(), GetCopper(), 0 };
	bool has_coin = ((money[PickPocketPlatinum] | money[PickPocketGold] | money[PickPocketSilver] | money[PickPocketCopper]) != 0);
	bool steal_item = (steal_skill >= steal_chance && (zone->random.Roll(50) || !has_coin));

	// still needs to have FindFreeSlot vs PutItemInInventory issue worked out
	while (steal_item) {
		std::vector<std::pair<const EQEmu::ItemData*, uint16>> loot_selection; // <const ItemData*, charges>
		for (auto item_iter : itemlist) {
			if (!item_iter || !item_iter->item_id)
				continue;

			auto item_test = database.GetItem(item_iter->item_id);
			if (item_test->Magic || !item_test->NoDrop || item_test->IsClassBag() || thief->CheckLoreConflict(item_test))
				continue;

			loot_selection.push_back(std::make_pair(item_test, ((item_test->Stackable) ? (1) : (item_iter->charges))));
		}
		if (loot_selection.empty()) {
			steal_item = false;
			break;
		}

		int random = zone->random.Int(0, (loot_selection.size() - 1));
		uint16 slot_id = thief->GetInv().FindFreeSlot(false, true, (loot_selection[random].first->Size), (loot_selection[random].first->ItemType == EQEmu::item::ItemTypeArrow));
		if (slot_id == INVALID_INDEX) {
			steal_item = false;
			break;
		}
		
		auto item_inst = database.CreateItem(loot_selection[random].first, loot_selection[random].second);
		if (item_inst == nullptr) {
			steal_item = false;
			break;
		}

		// Successful item pickpocket
		if (item_inst->IsStackable() && RuleB(Character, UseStackablePickPocketing)) {
			if (!thief->TryStacking(item_inst, ItemPacketTrade, false, false)) {
				thief->PutItemInInventory(slot_id, *item_inst);
				thief->SendItemPacket(slot_id, item_inst, ItemPacketTrade);
			}
		}
		else {
			thief->PutItemInInventory(slot_id, *item_inst);
			thief->SendItemPacket(slot_id, item_inst, ItemPacketTrade);
		}
		RemoveItem(item_inst->GetID());
		thief->SendPickPocketResponse(this, 0, PickPocketItem, item_inst->GetItem());

		return;
	}

	while (!steal_item && has_coin) {
		uint32 coin_amount = zone->random.Int(1, (steal_skill / 25) + 1);
		
		int coin_type = PickPocketPlatinum;
		while (coin_type <= PickPocketCopper) {
			if (money[coin_type]) {
				if (coin_amount > money[coin_type])
					coin_amount = money[coin_type];
				break;
			}
			++coin_type;
		}
		if (coin_type > PickPocketCopper)
			break;

		memset(money, 0, (sizeof(int) * 6));
		money[coin_type] = coin_amount;

		if (zone->random.Roll(steal_chance)) { // Successful coin pickpocket
			switch (coin_type) {
			case PickPocketPlatinum:
				SetPlatinum(GetPlatinum() - coin_amount);
				break;
			case PickPocketGold:
				SetGold(GetGold() - coin_amount);
				break;
			case PickPocketSilver:
				SetSilver(GetSilver() - coin_amount);
				break;
			case PickPocketCopper:
				SetCopper(GetCopper() - coin_amount);
				break;
			default: // has_coin..but, doesn't have coin?
				thief->SendPickPocketResponse(this, 0, PickPocketFailed);
				return;
			}

			thief->AddMoneyToPP(money[PickPocketCopper], money[PickPocketSilver], money[PickPocketGold], money[PickPocketPlatinum], false);
			thief->SendPickPocketResponse(this, coin_amount, coin_type);
			return;
		}

		thief->SendPickPocketResponse(this, 0, PickPocketFailed);
		return;
	}

	thief->Message(0, "This target's pockets are empty");
	thief->SendPickPocketResponse(this, 0, PickPocketFailed);
}

void Mob::NPCSpecialAttacks(const char* parse, int permtag, bool reset, bool remove) {
	if(reset)
	{
		ClearSpecialAbilities();
	}

	const char* orig_parse = parse;
	while (*parse)
	{
		switch(*parse)
		{
			case 'E':
				SetSpecialAbility(SPECATK_ENRAGE, remove ? 0 : 1);
				break;
			case 'F':
				SetSpecialAbility(SPECATK_FLURRY, remove ? 0 : 1);
				break;
			case 'R':
				SetSpecialAbility(SPECATK_RAMPAGE, remove ? 0 : 1);
				break;
			case 'r':
				SetSpecialAbility(SPECATK_AREA_RAMPAGE, remove ? 0 : 1);
				break;
			case 'S':
				if(remove) {
					SetSpecialAbility(SPECATK_SUMMON, 0);
					StopSpecialAbilityTimer(SPECATK_SUMMON);
				} else {
					SetSpecialAbility(SPECATK_SUMMON, 1);
				}
			break;
			case 'T':
				SetSpecialAbility(SPECATK_TRIPLE, remove ? 0 : 1);
				break;
			case 'Q':
				//quad requires triple to work properly
				if(remove) {
					SetSpecialAbility(SPECATK_QUAD, 0);
				} else {
					SetSpecialAbility(SPECATK_TRIPLE, 1);
					SetSpecialAbility(SPECATK_QUAD, 1);
					}
				break;
			case 'b':
				SetSpecialAbility(SPECATK_BANE, remove ? 0 : 1);
				break;
			case 'm':
				SetSpecialAbility(SPECATK_MAGICAL, remove ? 0 : 1);
				break;
			case 'U':
				SetSpecialAbility(UNSLOWABLE, remove ? 0 : 1);
				break;
			case 'M':
				SetSpecialAbility(UNMEZABLE, remove ? 0 : 1);
				break;
			case 'C':
				SetSpecialAbility(UNCHARMABLE, remove ? 0 : 1);
				break;
			case 'N':
				SetSpecialAbility(UNSTUNABLE, remove ? 0 : 1);
				break;
			case 'I':
				SetSpecialAbility(UNSNAREABLE, remove ? 0 : 1);
				break;
			case 'D':
				SetSpecialAbility(UNFEARABLE, remove ? 0 : 1);
				break;
			case 'K':
				SetSpecialAbility(UNDISPELLABLE, remove ? 0 : 1);
				break;
			case 'A':
				SetSpecialAbility(IMMUNE_MELEE, remove ? 0 : 1);
				break;
			case 'B':
				SetSpecialAbility(IMMUNE_MAGIC, remove ? 0 : 1);
				break;
			case 'f':
				SetSpecialAbility(IMMUNE_FLEEING, remove ? 0 : 1);
				break;
			case 'O':
				SetSpecialAbility(IMMUNE_MELEE_EXCEPT_BANE, remove ? 0 : 1);
				break;
			case 'W':
				SetSpecialAbility(IMMUNE_MELEE_NONMAGICAL, remove ? 0 : 1);
				break;
			case 'H':
				SetSpecialAbility(IMMUNE_AGGRO, remove ? 0 : 1);
				break;
			case 'G':
				SetSpecialAbility(IMMUNE_AGGRO_ON, remove ? 0 : 1);
				break;
			case 'g':
				SetSpecialAbility(IMMUNE_CASTING_FROM_RANGE, remove ? 0 : 1);
				break;
			case 'd':
				SetSpecialAbility(IMMUNE_FEIGN_DEATH, remove ? 0 : 1);
				break;
			case 'Y':
				SetSpecialAbility(SPECATK_RANGED_ATK, remove ? 0 : 1);
				break;
			case 'L':
				SetSpecialAbility(SPECATK_INNATE_DW, remove ? 0 : 1);
				break;
			case 't':
				SetSpecialAbility(NPC_TUNNELVISION, remove ? 0 : 1);
				break;
			case 'n':
				SetSpecialAbility(NPC_NO_BUFFHEAL_FRIENDS, remove ? 0 : 1);
				break;
			case 'p':
				SetSpecialAbility(IMMUNE_PACIFY, remove ? 0 : 1);
				break;
			case 'J':
				SetSpecialAbility(LEASH, remove ? 0 : 1);
				break;
			case 'j':
				SetSpecialAbility(TETHER, remove ? 0 : 1);
				break;
			case 'o':
				SetSpecialAbility(DESTRUCTIBLE_OBJECT, remove ? 0 : 1);
				SetDestructibleObject(remove ? true : false);
				break;
			case 'Z':
				SetSpecialAbility(NO_HARM_FROM_CLIENT, remove ? 0 : 1);
				break;
			case 'i':
				SetSpecialAbility(IMMUNE_TAUNT, remove ? 0 : 1);
				break;
			case 'e':
				SetSpecialAbility(ALWAYS_FLEE, remove ? 0 : 1);
				break;
			case 'h':
				SetSpecialAbility(FLEE_PERCENT, remove ? 0 : 1);
				break;

			default:
				break;
		}
		parse++;
	}

	if(permtag == 1 && this->GetNPCTypeID() > 0)
	{
		if(database.SetSpecialAttkFlag(this->GetNPCTypeID(), orig_parse))
		{
			Log(Logs::General, Logs::Normal, "NPCTypeID: %i flagged to '%s' for Special Attacks.\n",this->GetNPCTypeID(),orig_parse);
		}
	}
}

bool Mob::HasNPCSpecialAtk(const char* parse) {

	bool HasAllAttacks = true;

	while (*parse && HasAllAttacks == true)
	{
		switch(*parse)
		{
			case 'E':
				if (!GetSpecialAbility(SPECATK_ENRAGE))
					HasAllAttacks = false;
				break;
			case 'F':
				if (!GetSpecialAbility(SPECATK_FLURRY))
					HasAllAttacks = false;
				break;
			case 'R':
				if (!GetSpecialAbility(SPECATK_RAMPAGE))
					HasAllAttacks = false;
				break;
			case 'r':
				if (!GetSpecialAbility(SPECATK_AREA_RAMPAGE))
					HasAllAttacks = false;
				break;
			case 'S':
				if (!GetSpecialAbility(SPECATK_SUMMON))
					HasAllAttacks = false;
				break;
			case 'T':
				if (!GetSpecialAbility(SPECATK_TRIPLE))
					HasAllAttacks = false;
				break;
			case 'Q':
				if (!GetSpecialAbility(SPECATK_QUAD))
					HasAllAttacks = false;
				break;
			case 'b':
				if (!GetSpecialAbility(SPECATK_BANE))
					HasAllAttacks = false;
				break;
			case 'm':
				if (!GetSpecialAbility(SPECATK_MAGICAL))
					HasAllAttacks = false;
				break;
			case 'U':
				if (!GetSpecialAbility(UNSLOWABLE))
					HasAllAttacks = false;
				break;
			case 'M':
				if (!GetSpecialAbility(UNMEZABLE))
					HasAllAttacks = false;
				break;
			case 'C':
				if (!GetSpecialAbility(UNCHARMABLE))
					HasAllAttacks = false;
				break;
			case 'N':
				if (!GetSpecialAbility(UNSTUNABLE))
					HasAllAttacks = false;
				break;
			case 'I':
				if (!GetSpecialAbility(UNSNAREABLE))
					HasAllAttacks = false;
				break;
			case 'D':
				if (!GetSpecialAbility(UNFEARABLE))
					HasAllAttacks = false;
				break;
			case 'A':
				if (!GetSpecialAbility(IMMUNE_MELEE))
					HasAllAttacks = false;
				break;
			case 'B':
				if (!GetSpecialAbility(IMMUNE_MAGIC))
					HasAllAttacks = false;
				break;
			case 'f':
				if (!GetSpecialAbility(IMMUNE_FLEEING))
					HasAllAttacks = false;
				break;
			case 'O':
				if (!GetSpecialAbility(IMMUNE_MELEE_EXCEPT_BANE))
					HasAllAttacks = false;
				break;
			case 'W':
				if (!GetSpecialAbility(IMMUNE_MELEE_NONMAGICAL))
					HasAllAttacks = false;
				break;
			case 'H':
				if (!GetSpecialAbility(IMMUNE_AGGRO))
					HasAllAttacks = false;
				break;
			case 'G':
				if (!GetSpecialAbility(IMMUNE_AGGRO_ON))
					HasAllAttacks = false;
				break;
			case 'g':
				if (!GetSpecialAbility(IMMUNE_CASTING_FROM_RANGE))
					HasAllAttacks = false;
				break;
			case 'd':
				if (!GetSpecialAbility(IMMUNE_FEIGN_DEATH))
					HasAllAttacks = false;
				break;
			case 'Y':
				if (!GetSpecialAbility(SPECATK_RANGED_ATK))
					HasAllAttacks = false;
				break;
			case 'L':
				if (!GetSpecialAbility(SPECATK_INNATE_DW))
					HasAllAttacks = false;
				break;
			case 't':
				if (!GetSpecialAbility(NPC_TUNNELVISION))
					HasAllAttacks = false;
				break;
			case 'n':
				if (!GetSpecialAbility(NPC_NO_BUFFHEAL_FRIENDS))
					HasAllAttacks = false;
				break;
			case 'p':
				if(!GetSpecialAbility(IMMUNE_PACIFY))
					HasAllAttacks = false;
				break;
			case 'J':
				if(!GetSpecialAbility(LEASH))
					HasAllAttacks = false;
				break;
			case 'j':
				if(!GetSpecialAbility(TETHER))
					HasAllAttacks = false;
				break;
			case 'o':
				if(!GetSpecialAbility(DESTRUCTIBLE_OBJECT))
				{
					HasAllAttacks = false;
					SetDestructibleObject(false);
				}
				break;
			case 'Z':
				if(!GetSpecialAbility(NO_HARM_FROM_CLIENT)){
					HasAllAttacks = false;
				}
				break;
			case 'e':
				if(!GetSpecialAbility(ALWAYS_FLEE))
					HasAllAttacks = false;
				break;
			case 'h':
				if(!GetSpecialAbility(FLEE_PERCENT))
					HasAllAttacks = false;
				break;
			default:
				HasAllAttacks = false;
				break;
		}
		parse++;
	}

	return HasAllAttacks;
}

void NPC::FillSpawnStruct(NewSpawn_Struct* ns, Mob* ForWho)
{
	Mob::FillSpawnStruct(ns, ForWho);
	PetOnSpawn(ns);
	ns->spawn.is_npc = 1;
	UpdateActiveLight();
	ns->spawn.light = GetActiveLightType();
	ns->spawn.show_name = NPCTypedata->show_name;
}

void NPC::PetOnSpawn(NewSpawn_Struct* ns)
{
	//Basic settings to make sure swarm pets work properly.
	Mob *swarmOwner = nullptr;
	if  (GetSwarmOwner())
	{
		swarmOwner = entity_list.GetMobID(GetSwarmOwner());
	}

	if  (swarmOwner != nullptr)
	{
		if(swarmOwner->IsClient())
		{
			SetPetOwnerClient(true); //Simple flag to determine if pet belongs to a client
			SetAllowBeneficial(true);//Allow temp pets to receive buffs and heals if owner is client.
			//This will allow CLIENT swarm pets NOT to be targeted with F8.
			ns->spawn.targetable_with_hotkey = 0;
			no_target_hotkey = 1;
		}
		else
		{
			//NPC cast swarm pets should still be targetable with F8.
			ns->spawn.targetable_with_hotkey = 1;
			no_target_hotkey = 0;
		}

		SetTempPet(true); //Simple mob flag for checking if temp pet
		swarmOwner->SetTempPetsActive(true); //Necessary fail safe flag set if mob ever had a swarm pet to ensure they are removed.
		swarmOwner->SetTempPetCount(swarmOwner->GetTempPetCount() + 1);

		//Not recommended if using above (However, this will work better on older clients).
		if (RuleB(Pets, UnTargetableSwarmPet))
		{
			ns->spawn.bodytype = 11;
			if(!IsCharmed() && swarmOwner->IsClient())
				sprintf(ns->spawn.lastName, "%s's Pet", swarmOwner->GetName());
		}
	}
	else if(GetOwnerID())
	{
		ns->spawn.is_pet = 1;
		if (!IsCharmed())
		{
			Client *client = entity_list.GetClientByID(GetOwnerID());
			if(client)
			{
				SetPetOwnerClient(true);
				sprintf(ns->spawn.lastName, "%s's Pet", client->GetName());
			}
		}
	}
	else
	{
		ns->spawn.is_pet = 0;
	}
}

void NPC::SetLevel(uint8 in_level, bool command)
{
	if(in_level > level)
		SendLevelAppearance();
	level = in_level;
	SendAppearancePacket(AT_WhoLevel, in_level);
}

void NPC::ModifyNPCStat(const char *identifier, const char *newValue)
{
	std::string id = identifier;
	std::string val = newValue;
	for(int i = 0; i < id.length(); ++i) {
		id[i] = std::tolower(id[i]);
	}

	if(id == "ac") { AC = atoi(val.c_str()); CalcAC(); return; }
	else if(id == "str") { STR = atoi(val.c_str()); return; }
	else if(id == "sta") { STA = atoi(val.c_str()); return; }
	else if(id == "agi") { AGI = atoi(val.c_str()); CalcAC(); return; }
	else if(id == "dex") { DEX = atoi(val.c_str()); return; }
	else if(id == "wis") { WIS = atoi(val.c_str()); CalcMaxMana(); return; }
	else if(id == "int" || id == "_int") { INT = atoi(val.c_str()); CalcMaxMana(); return; }
	else if(id == "cha") { CHA = atoi(val.c_str()); return; }
	else if(id == "max_hp") { base_hp = atoi(val.c_str()); CalcMaxHP(); if (cur_hp > max_hp) { cur_hp = max_hp; } return; }
	else if(id == "max_mana") { npc_mana = atoi(val.c_str()); CalcMaxMana(); if (current_mana > max_mana){ current_mana = max_mana; } return; }
	else if(id == "mr") { MR = atoi(val.c_str()); return; }
	else if(id == "fr") { FR = atoi(val.c_str()); return; }
	else if(id == "cr") { CR = atoi(val.c_str()); return; }
	else if(id == "pr") { PR = atoi(val.c_str()); return; }
	else if(id == "dr") { DR = atoi(val.c_str()); return; }
	else if(id == "phr") { PhR = atoi(val.c_str()); return; }
	else if(id == "runspeed") {
		runspeed = (float)atof(val.c_str());
		base_runspeed = (int)((float)runspeed * 40.0f);
		base_walkspeed = base_runspeed * 100 / 265;
		walkspeed = ((float)base_walkspeed) * 0.025f;
		base_fearspeed = base_runspeed * 100 / 127;
		fearspeed = ((float)base_fearspeed) * 0.025f;
		CalcBonuses(); return;
	}
	else if(id == "special_attacks") { NPCSpecialAttacks(val.c_str(), 0, 1); return; }
	else if(id == "special_abilities") { ProcessSpecialAbilities(val.c_str()); return; }
	else if(id == "attack_speed") { attack_speed = (float)atof(val.c_str()); CalcBonuses(); return; }
	else if(id == "attack_delay") { /* TODO: fix DB */attack_delay = atoi(val.c_str()) * 100; CalcBonuses(); return; }
	else if(id == "atk") { ATK = atoi(val.c_str()); return; }
	else if(id == "accuracy") { accuracy_rating = atoi(val.c_str()); return; }
	else if(id == "avoidance") { avoidance_rating = atoi(val.c_str()); return; }
	else if(id == "trackable") { trackable = atoi(val.c_str()); return; }
	else if(id == "min_hit") {
		min_dmg = atoi(val.c_str());
		// TODO: fix DB
		base_damage = round((max_dmg - min_dmg) / 1.9);
		min_damage = min_dmg - round(base_damage / 10.0);
		return;
	}
	else if(id == "max_hit") {
		max_dmg = atoi(val.c_str());
		// TODO: fix DB
		base_damage = round((max_dmg - min_dmg) / 1.9);
		min_damage = min_dmg - round(base_damage / 10.0);
		return;
	}
	else if(id == "attack_count") { attack_count = atoi(val.c_str()); return; }
	else if(id == "see_invis") { see_invis = atoi(val.c_str()); return; }
	else if(id == "see_invis_undead") { see_invis_undead = atoi(val.c_str()); return; }
	else if(id == "see_hide") { see_hide = atoi(val.c_str()); return; }
	else if(id == "see_improved_hide") { see_improved_hide = atoi(val.c_str()); return; }
	else if(id == "hp_regen") { hp_regen = atoi(val.c_str()); return; }
	else if(id == "mana_regen") { mana_regen = atoi(val.c_str()); return; }
	else if(id == "level") { SetLevel(atoi(val.c_str())); return; }
	else if(id == "aggro") { pAggroRange = atof(val.c_str()); return; }
	else if(id == "assist") { pAssistRange = atof(val.c_str()); return; }
	else if(id == "slow_mitigation") { slow_mitigation = atoi(val.c_str()); return; }
	else if(id == "loottable_id") { loottable_id = atof(val.c_str()); return; }
	else if(id == "healscale") { healscale = atof(val.c_str()); return; }
	else if(id == "spellscale") { spellscale = atof(val.c_str()); return; }
	else if(id == "npc_spells_id") { AI_AddNPCSpells(atoi(val.c_str())); return; }
	else if(id == "npc_spells_effects_id") { AI_AddNPCSpellsEffects(atoi(val.c_str())); CalcBonuses(); return; }
}

void NPC::LevelScale() {

	uint8 random_level = (zone->random.Int(level, maxlevel));

	float scaling = (((random_level / (float)level) - 1) * (scalerate / 100.0f));

	if (RuleB(NPC, NewLevelScaling)) {
		if (scalerate == 0 || maxlevel <= 25) {
			// pre-pop seems to scale by 20 HP increments while newer by 100
			// We also don't want 100 increments on newer noobie zones, check level
			if (zone->GetZoneID() < 200 || level < 48) {
				max_hp += (random_level - level) * 20;
				base_hp += (random_level - level) * 20;
			} else {
				max_hp += (random_level - level) * 100;
				base_hp += (random_level - level) * 100;
			}

			cur_hp = max_hp;
			max_dmg += (random_level - level) * 2;
		} else {
			uint8 scale_adjust = 1;

			base_hp += (int)(base_hp * scaling);
			max_hp += (int)(max_hp * scaling);
			cur_hp = max_hp;

			if (max_dmg) {
				max_dmg += (int)(max_dmg * scaling / scale_adjust);
				min_dmg += (int)(min_dmg * scaling / scale_adjust);
			}

			STR += (int)(STR * scaling / scale_adjust);
			STA += (int)(STA * scaling / scale_adjust);
			AGI += (int)(AGI * scaling / scale_adjust);
			DEX += (int)(DEX * scaling / scale_adjust);
			INT += (int)(INT * scaling / scale_adjust);
			WIS += (int)(WIS * scaling / scale_adjust);
			CHA += (int)(CHA * scaling / scale_adjust);
			if (MR)
				MR += (int)(MR * scaling / scale_adjust);
			if (CR)
				CR += (int)(CR * scaling / scale_adjust);
			if (DR)
				DR += (int)(DR * scaling / scale_adjust);
			if (FR)
				FR += (int)(FR * scaling / scale_adjust);
			if (PR)
				PR += (int)(PR * scaling / scale_adjust);
		}
	} else {
		// Compensate for scale rates at low levels so they don't add too much
		uint8 scale_adjust = 1;
		if(level > 0 && level <= 5)
			scale_adjust = 10;
		if(level > 5 && level <= 10)
			scale_adjust = 5;
		if(level > 10 && level <= 15)
			scale_adjust = 3;
		if(level > 15 && level <= 25)
			scale_adjust = 2;

		AC += (int)(AC * scaling);
		ATK += (int)(ATK * scaling);
		base_hp += (int)(base_hp * scaling);
		max_hp += (int)(max_hp * scaling);
		cur_hp = max_hp;
		STR += (int)(STR * scaling / scale_adjust);
		STA += (int)(STA * scaling / scale_adjust);
		AGI += (int)(AGI * scaling / scale_adjust);
		DEX += (int)(DEX * scaling / scale_adjust);
		INT += (int)(INT * scaling / scale_adjust);
		WIS += (int)(WIS * scaling / scale_adjust);
		CHA += (int)(CHA * scaling / scale_adjust);
		if (MR)
			MR += (int)(MR * scaling / scale_adjust);
		if (CR)
			CR += (int)(CR * scaling / scale_adjust);
		if (DR)
			DR += (int)(DR * scaling / scale_adjust);
		if (FR)
			FR += (int)(FR * scaling / scale_adjust);
		if (PR)
			PR += (int)(PR * scaling / scale_adjust);

		if (max_dmg)
		{
			max_dmg += (int)(max_dmg * scaling / scale_adjust);
			min_dmg += (int)(min_dmg * scaling / scale_adjust);
		}

	}
	level = random_level;

	return;
}

void NPC::CalcNPCResists() {

	if (!MR)
		MR = (GetLevel() * 11)/10;
	if (!CR)
		CR = (GetLevel() * 11)/10;
	if (!DR)
		DR = (GetLevel() * 11)/10;
	if (!FR)
		FR = (GetLevel() * 11)/10;
	if (!PR)
		PR = (GetLevel() * 11)/10;
	if (!Corrup)
		Corrup = 15;
	if (!PhR)
		PhR = 10;
	return;
}

void NPC::CalcNPCRegen() {

	// Fix for lazy db-updaters (regen values left at 0)
	if (GetCasterClass() != 'N' && mana_regen == 0)
		mana_regen = (GetLevel() / 10) + 4;
	else if(mana_regen < 0)
		mana_regen = 0;
	else
		mana_regen = mana_regen;

	// Gives low end monsters no regen if set to 0 in database. Should make low end monsters killable
	// Might want to lower this to /5 rather than 10.
	if(hp_regen == 0)
	{
		if(GetLevel() <= 6)
			hp_regen = 1;
		else if(GetLevel() > 6 && GetLevel() <= 10)
			hp_regen = 2;
		else if(GetLevel() > 10 && GetLevel() <= 15)
			hp_regen = 3;
		else if(GetLevel() > 15 && GetLevel() <= 20)
			hp_regen = 5;
		else if(GetLevel() > 20 && GetLevel() <= 30)
			hp_regen = 7;
		else if(GetLevel() > 30 && GetLevel() <= 35)
			hp_regen = 9;
		else if(GetLevel() > 35 && GetLevel() <= 40)
			hp_regen = 12;
		else if(GetLevel() > 40 && GetLevel() <= 45)
			hp_regen = 18;
		else if(GetLevel() > 45 && GetLevel() <= 50)
			hp_regen = 21;
		else
			hp_regen = 30;
	} else if(hp_regen < 0) {
		hp_regen = 0;
	} else
		hp_regen = hp_regen;

	return;
}

void NPC::CalcNPCDamage() {

	int AC_adjust=12;

	if (GetLevel() >= 60){
		if(min_dmg==0)
			min_dmg = (GetLevel()+(GetLevel()/3));
		if (max_dmg == 0) 
			max_dmg = (GetLevel() * 3)*AC_adjust / 10;
	}
	else if (GetLevel() >= 51 && GetLevel() <= 59){
		if(min_dmg==0)
			min_dmg = (GetLevel()+(GetLevel()/3));
		if(max_dmg==0)
			max_dmg = (GetLevel()*3)*AC_adjust/10;
	}
	else if (GetLevel() >= 40 && GetLevel() <= 50) {
		if (min_dmg==0)
			min_dmg = GetLevel();
		if(max_dmg==0)
			max_dmg = (GetLevel()*3)*AC_adjust/10;
	}
	else if (GetLevel() >= 28 && GetLevel() <= 39) {
		if (min_dmg==0)
			min_dmg = GetLevel() / 2;
		if (max_dmg==0)
			max_dmg = ((GetLevel()*2)+2)*AC_adjust/10;
	}
	else if (GetLevel() <= 27) {
		if (min_dmg==0)
			min_dmg=1;
		if (max_dmg==0)
			max_dmg = (GetLevel()*2)*AC_adjust/10;
	}

	int32 clfact = GetClassLevelFactor();
	min_dmg = (min_dmg * clfact) / 220;
	max_dmg = (max_dmg * clfact) / 220;

	min_dmg = min_dmg / RuleI(LoC, MinDamageDivider);
	max_dmg = max_dmg / RuleI(LoC, MaxDamageDivider);
	return;
}


uint32 NPC::GetSpawnPointID() const
{
	if(respawn2)
	{
		return respawn2->GetID();
	}
	return 0;
}

void NPC::NPCSlotTexture(uint8 slot, uint16 texture)
{
	if (slot == 7) {
		d_melee_texture1 = texture;
	}
	else if (slot == 8) {
		d_melee_texture2 = texture;
	}
	else if (slot < 6) {
		// Reserved for texturing individual armor slots
	}
	return;
}

uint32 NPC::GetSwarmOwner()
{
	if(GetSwarmInfo() != nullptr)
	{
		return GetSwarmInfo()->owner_id;
	}
	return 0;
}

uint32 NPC::GetSwarmTarget()
{
	if(GetSwarmInfo() != nullptr)
	{
		return GetSwarmInfo()->target;
	}
	return 0;
}

void NPC::SetSwarmTarget(int target_id)
{
	if(GetSwarmInfo() != nullptr)
	{
		GetSwarmInfo()->target = target_id;
	}
	return;
}

int32 NPC::CalcMaxMana() {
	if(npc_mana == 0) {
		switch (GetCasterClass()) {
			case 'I':
				max_mana = (((GetINT()/2)+1) * GetLevel()) + spellbonuses.Mana + itembonuses.Mana;
				break;
			case 'W':
				max_mana = (((GetWIS()/2)+1) * GetLevel()) + spellbonuses.Mana + itembonuses.Mana;
				break;
			case 'N':
			default:
				max_mana = 0;
				break;
		}
		if (max_mana < 0) {
			max_mana = 0;
		}

		return max_mana;
	} else {
		switch (GetCasterClass()) {
			case 'I':
				max_mana = npc_mana + spellbonuses.Mana + itembonuses.Mana;
				break;
			case 'W':
				max_mana = npc_mana + spellbonuses.Mana + itembonuses.Mana;
				break;
			case 'N':
			default:
				max_mana = 0;
				break;
		}
		if (max_mana < 0) {
			max_mana = 0;
		}

		return max_mana;
	}
}

void NPC::SignalNPC(int _signal_id)
{
	signal_q.push_back(_signal_id);
}

NPC_Emote_Struct* NPC::GetNPCEmote(uint16 emoteid, uint8 event_) {
	LinkedListIterator<NPC_Emote_Struct*> iterator(zone->NPCEmoteList);
	iterator.Reset();
	while(iterator.MoreElements())
	{
		NPC_Emote_Struct* nes = iterator.GetData();
		if (emoteid == nes->emoteid && event_ == nes->event_) {
			return (nes);
		}
		iterator.Advance();
	}
	return (nullptr);
}

void NPC::DoNPCEmote(uint8 event_, uint16 emoteid)
{
	if(this == nullptr || emoteid == 0)
	{
		return;
	}

	NPC_Emote_Struct* nes = GetNPCEmote(emoteid,event_);
	if(nes == nullptr)
	{
		return;
	}

	if(emoteid == nes->emoteid)
	{
		if(nes->type == 1)
			this->Emote("%s",nes->text);
		else if(nes->type == 2)
			this->Shout("%s",nes->text);
		else if(nes->type == 3)
			entity_list.MessageClose_StringID(this, true, 200, 10, GENERIC_STRING, nes->text);
		else
			this->Say("%s",nes->text);
	}
}

bool NPC::CanTalk()
{
	//Races that should be able to talk. (Races up to Titanium)

	uint16 TalkRace[473] =
	{1,2,3,4,5,6,7,8,9,10,11,12,0,0,15,16,0,18,19,20,0,0,23,0,25,0,0,0,0,0,0,
	32,0,0,0,0,0,0,39,40,0,0,0,44,0,0,0,0,49,0,51,0,53,54,55,56,57,58,0,0,0,
	62,0,64,65,66,67,0,0,70,71,0,0,0,0,0,77,78,79,0,81,82,0,0,0,86,0,0,0,90,
	0,92,93,94,95,0,0,98,99,0,101,0,103,0,0,0,0,0,0,110,111,112,0,0,0,0,0,0,
	0,0,0,0,123,0,0,126,0,128,0,130,131,0,0,0,0,136,137,0,139,140,0,0,0,144,
	0,0,0,0,0,150,151,152,153,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,183,184,0,0,187,188,189,0,0,0,0,0,195,196,0,198,0,0,0,202,0,
	0,205,0,0,208,0,0,0,0,0,0,0,0,217,0,219,0,0,0,0,0,0,226,0,0,229,230,0,0,
	0,0,235,236,0,238,239,240,241,242,243,244,0,246,247,0,0,0,251,0,0,254,255,
	256,257,0,0,0,0,0,0,0,0,266,267,0,0,270,271,0,0,0,0,0,277,278,0,0,0,0,283,
	284,0,286,0,288,289,290,0,0,0,0,295,296,297,298,299,300,0,0,0,304,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,320,0,322,323,324,325,0,0,0,0,330,331,332,333,334,335,
	336,337,338,339,340,341,342,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,359,360,361,362,
	0,364,365,366,0,368,369,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,385,386,0,0,0,0,0,392,
	393,394,395,396,397,398,0,400,402,0,0,0,0,406,0,408,0,0,411,0,413,0,0,0,417,
	0,0,420,0,0,0,0,425,0,0,0,0,0,0,0,433,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,458,0,0,0,0,0,0,0,0,467,0,0,470,0,0,473};

	if (TalkRace[GetRace() - 1] > 0)
		return true;

	return false;
}

//this is called with 'this' as the mob being looked at, and
//iOther the mob who is doing the looking. It should figure out
//what iOther thinks about 'this'
FACTION_VALUE NPC::GetReverseFactionCon(Mob* iOther) {
	iOther = iOther->GetOwnerOrSelf();
	int primaryFaction= iOther->GetPrimaryFaction();

	//I am pretty sure that this special faction call is backwards
	//and should be iOther->GetSpecialFactionCon(this)
	if (primaryFaction < 0)
		return GetSpecialFactionCon(iOther);

	if (primaryFaction == 0)
		return FACTION_INDIFFERENT;

	//if we are a pet, use our owner's faction stuff
	Mob *own = GetOwner();
	if (own != nullptr)
		return own->GetReverseFactionCon(iOther);

	//make sure iOther is an npc
	//also, if we dont have a faction, then they arnt gunna think anything of us either
	if(!iOther->IsNPC() || GetPrimaryFaction() == 0)
		return(FACTION_INDIFFERENT);

	//if we get here, iOther is an NPC too

	//otherwise, employ the npc faction stuff
	//so we need to look at iOther's faction table to see
	//what iOther thinks about our primary faction
	return(iOther->CastToNPC()->CheckNPCFactionAlly(GetPrimaryFaction()));
}

//Look through our faction list and return a faction con based
//on the npc_value for the other person's primary faction in our list.
FACTION_VALUE NPC::CheckNPCFactionAlly(int32 other_faction) {
	std::list<struct NPCFaction*>::iterator cur,end;
	cur = faction_list.begin();
	end = faction_list.end();
	for(; cur != end; ++cur) {
		struct NPCFaction* fac = *cur;
		if ((int32)fac->factionID == other_faction) {
			if (fac->npc_value > 0)
				return FACTION_ALLY;
			else if (fac->npc_value < 0)
				return FACTION_SCOWLS;
			else
				return FACTION_INDIFFERENT;
		}
	}
	return FACTION_INDIFFERENT;
}

bool NPC::IsFactionListAlly(uint32 other_faction) {
	return(CheckNPCFactionAlly(other_faction) == FACTION_ALLY);
}

int NPC::GetScore()
{
    int lv = std::min(70, (int)GetLevel());
    int basedmg = (lv*2)*(1+(lv / 100)) - (lv / 2);
    int minx = 0;
    int basehp = 0;
    int hpcontrib = 0;
    int dmgcontrib = 0;
    int spccontrib = 0;
    int hp = GetMaxHP();
    int mindmg = min_dmg;
    int maxdmg = max_dmg;
    int final;

    if(lv < 46)
    {
		minx = static_cast<int> (ceil( ((lv - (lv / 10.0)) - 1.0) ));
		basehp = (lv * 10) + (lv * lv);
	}
	else
	{
		minx = static_cast<int> (ceil( ((lv - (lv / 10.0)) - 1.0) - (( lv - 45.0 ) / 2.0) ));
        basehp = (lv * 10) + ((lv * lv) * 4);
    }

    if(hp > basehp)
    {
        hpcontrib = static_cast<int> (((hp / static_cast<float> (basehp)) * 1.5));
        if(hpcontrib > 5) { hpcontrib = 5; }

        if(maxdmg > basedmg)
        {
            dmgcontrib = static_cast<int> (ceil( ((maxdmg / basedmg) * 1.5) ));
        }

        if(HasNPCSpecialAtk("E")) { spccontrib++; }    //Enrage
        if(HasNPCSpecialAtk("F")) { spccontrib++; }    //Flurry
        if(HasNPCSpecialAtk("R")) { spccontrib++; }    //Rampage
        if(HasNPCSpecialAtk("r")) { spccontrib++; }    //Area Rampage
        if(HasNPCSpecialAtk("S")) { spccontrib++; }    //Summon
        if(HasNPCSpecialAtk("T")) { spccontrib += 2; } //Triple
        if(HasNPCSpecialAtk("Q")) { spccontrib += 3; } //Quad
        if(HasNPCSpecialAtk("U")) { spccontrib += 5; } //Unslowable
        if(HasNPCSpecialAtk("L")) { spccontrib++; }    //Innate Dual Wield
    }

    if(npc_spells_id > 12)
	{
        if(lv < 16)
            spccontrib++;
        else
            spccontrib += static_cast<int> (floor(lv/15.0));
    }

    final = minx + hpcontrib + dmgcontrib + spccontrib;
    final = std::max(1, final);
    final = std::min(100, final);
    return(final);
}

uint32 NPC::GetSpawnKillCount()
{
	uint32 sid = GetSpawnPointID();

	if(sid > 0)
	{
		return(zone->GetSpawnKillCount(sid));
	}

	return(0);
}

void NPC::DoQuestPause(Mob *other) {
	if(IsMoving() && !IsOnHatelist(other)) {
		PauseWandering(RuleI(NPC, SayPauseTimeInSec));
		FaceTarget(other);
	} else if(!IsMoving()) {
		FaceTarget(other);
	}

}

void NPC::ChangeLastName(const char* in_lastname)
{

	auto outapp = new EQApplicationPacket(OP_GMLastName, sizeof(GMLastName_Struct));
	GMLastName_Struct* gmn = (GMLastName_Struct*)outapp->pBuffer;
	strcpy(gmn->name, GetName());
	strcpy(gmn->gmname, GetName());
	strcpy(gmn->lastname, in_lastname);
	gmn->unknown[0]=1;
	gmn->unknown[1]=1;
	gmn->unknown[2]=1;
	gmn->unknown[3]=1;
	entity_list.QueueClients(this, outapp, false);
	safe_delete(outapp);
}

void NPC::ClearLastName()
{
	std::string WT;
	WT = '\0'; //Clear Last Name
	ChangeLastName( WT.c_str());
}

void NPC::DepopSwarmPets()
{

	if (GetSwarmInfo()) {
		if (GetSwarmInfo()->duration->Check(false)){
			Mob* owner = entity_list.GetMobID(GetSwarmInfo()->owner_id);
			if (owner)
				owner->SetTempPetCount(owner->GetTempPetCount() - 1);

			Depop();
			return;
		}

		//This is only used for optional quest or rule derived behavior now if you force a temp pet on a specific target.
		if (GetSwarmInfo()->target) {
			Mob *targMob = entity_list.GetMob(GetSwarmInfo()->target);
			if(!targMob || (targMob && targMob->IsCorpse())){
				Mob* owner = entity_list.GetMobID(GetSwarmInfo()->owner_id);
				if (owner)
					owner->SetTempPetCount(owner->GetTempPetCount() - 1);

				Depop();
				return;
			}
		}
	}

	if (IsPet() && GetPetType() == petTargetLock && GetPetTargetLockID()){

		Mob *targMob = entity_list.GetMob(GetPetTargetLockID());

		if(!targMob || (targMob && targMob->IsCorpse())){
			Kill();
			return;
		}
	}
}

void NPC::ModifyStatsOnCharm(bool bRemoved)
{
	if (bRemoved) {
		if (charm_ac)
			AC = default_ac;
		if (charm_attack_delay)
			attack_delay = default_attack_delay;
		if (charm_accuracy_rating)
			accuracy_rating = default_accuracy_rating;
		if (charm_avoidance_rating)
			avoidance_rating = default_avoidance_rating;
		if (charm_atk)
			ATK = default_atk;
		if (charm_min_dmg || charm_max_dmg) {
			base_damage = round((default_max_dmg - default_min_dmg) / 1.9);
			min_damage = default_min_dmg - round(base_damage / 10.0);
		}
	} else {
		if (charm_ac)
			AC = charm_ac;
		if (charm_attack_delay)
			attack_delay = charm_attack_delay;
		if (charm_accuracy_rating)
			accuracy_rating = charm_accuracy_rating;
		if (charm_avoidance_rating)
			avoidance_rating = charm_avoidance_rating;
		if (charm_atk)
			ATK = charm_atk;
		if (charm_min_dmg || charm_max_dmg) {
			base_damage = round((charm_max_dmg - charm_min_dmg) / 1.9);
			min_damage = charm_min_dmg - round(base_damage / 10.0);
		}
	}
	// the rest of the stats aren't cached, so lets just do these two instead of full CalcBonuses()
	SetAttackTimer();
	CalcAC();
}

//Hijack the spawning of an npc to inject new RNG system
void NPC::Hijack(const NPCType* d, Spawn2* in_respawn) {
	int rngType = GetRandomizeType(d, in_respawn);
	if (!rngType) return;

	uint8 cat = RandomizeCategory(d, in_respawn);
	ChangeCategory(cat);
	//stripping default spells
	npc_spells_id = 0;
	npc_spells_effects_id = 0;
	AdjustStats(d, in_respawn);
	AddAbilities(d, in_respawn);
	
}


//GetRandomize type returns what type of randomization should be done to the mob. 
//Currently, we just return 1 if the mob is a roaming monster.
int NPC::GetRandomizeType(const NPCType* d, Spawn2* in_respawn) {
	if (!in_respawn) return 0;
	uint32 sgid = in_respawn->SpawnGroupID();
	//toxx
	if (sgid == 4928 || sgid == 4941 || sgid == 4943 || sgid == 4944 || sgid == 4945 || sgid == 4950 || sgid == 4951 || sgid == 4953 || sgid == 4954 || sgid == 4955 || sgid == 4956 || sgid == 4957 || sgid == 4958 || sgid == 4959 || sgid == 4960 || sgid == 4961 || sgid == 4962 || sgid == 4963 || sgid == 4965 || sgid == 4966 || sgid == 4967 || sgid == 4968 || sgid == 4969 || sgid == 4970 || sgid == 4971 || sgid == 4972 || sgid == 4973 || sgid == 4974 || sgid == 4975 || sgid == 4983 || sgid == 4993 || sgid == 4997 || sgid == 5963 || sgid == 5966 || sgid == 5967 || sgid == 5983 || sgid == 48064 || sgid == 48344 || sgid == 99005 || sgid == 99006 || sgid == 99007 || sgid == 99008 || sgid == 99009 || sgid == 99010 || sgid == 99011 || sgid == 99012 || sgid == 99013 || sgid == 99014 || sgid == 99015 || sgid == 99017 || sgid == 99018 || sgid == 99019 || sgid == 99020 || sgid == 99021 || sgid == 99022 || sgid == 99023 || sgid == 99024 || sgid == 99025 || sgid == 99026 || sgid == 99027 || sgid == 99028 || sgid == 99029 || sgid == 99030 || sgid == 99031 || sgid == 99032 || sgid == 99033 || sgid == 99034 || sgid == 99035 || sgid == 99038 || sgid == 99039 || sgid == 99040 || sgid == 99041 || sgid == 99042 || sgid == 99043 || sgid == 99044 || sgid == 99045 || sgid == 99046 || sgid == 99047 || sgid == 99048 || sgid == 99049 || sgid == 99050 || sgid == 99051 || sgid == 99052 || sgid == 99053 || sgid == 99054 || sgid == 99055 || sgid == 99056 || sgid == 99057 || sgid == 275108) return 0;
	//befallen
	if (sgid == 112 || sgid == 6831 || sgid == 13103 || sgid == 48348) return 0;
	if (strcmp(zone->GetShortName(), "qrg") == 0) return 0;

	if (d->class_ > 16) return 0; //weird classes are not randomized
	return 1;
}




//Figure out what category the mob fits under
uint8 NPC::RandomizeCategory(const NPCType* d, Spawn2* in_respawn) {
	int normal_chance = 0;
	if (zone->GetInstanceID() == 0) normal_chance = RuleI(NPC, NormalCategoryChanceNormal);	
	if (zone->GetInstanceID() == 1) normal_chance = RuleI(NPC, NormalCategoryChanceNightmare);
	if (zone->GetInstanceID() == 2) normal_chance = RuleI(NPC, NormalCategoryChanceHell);

	int champion_chance = 0;
	if (zone->GetInstanceID() == 0) champion_chance = RuleI(NPC, ChampionCategoryChanceNormal);
	if (zone->GetInstanceID() == 1) champion_chance = RuleI(NPC, ChampionCategoryChanceNightmare);
	if (zone->GetInstanceID() == 2) champion_chance = RuleI(NPC, ChampionCategoryChanceHell);

	int rare_chance = 0;
	if (zone->GetInstanceID() == 0) rare_chance = RuleI(NPC, RareCategoryChanceNormal);
	if (zone->GetInstanceID() == 1) rare_chance = RuleI(NPC, RareCategoryChanceNightmare);
	if (zone->GetInstanceID() == 2) rare_chance = RuleI(NPC, RareCategoryChanceHell);

	int unique_chance = 0;
	if (zone->GetInstanceID() == 0) rare_chance = RuleI(NPC, UniqueCategoryChanceNormal);
	if (zone->GetInstanceID() == 1) rare_chance = RuleI(NPC, UniqueCategoryChanceNightmare);
	if (zone->GetInstanceID() == 2) rare_chance = RuleI(NPC, UniqueCategoryChanceHell);

	int super_unique_chance = 0;
	if (zone->GetInstanceID() == 0) super_unique_chance = RuleI(NPC, SuperUniqueCategoryChanceNormal);
	if (zone->GetInstanceID() == 1) super_unique_chance = RuleI(NPC, SuperUniqueCategoryChanceNightmare);
	if (zone->GetInstanceID() == 2) super_unique_chance = RuleI(NPC, SuperUniqueCategoryChanceHell);

	int boss_chance = 0;
	if (zone->GetInstanceID() == 0) boss_chance = RuleI(NPC, BossCategoryChanceNormal);
	if (zone->GetInstanceID() == 1) boss_chance = RuleI(NPC, BossCategoryChanceNightmare);
	if (zone->GetInstanceID() == 2) boss_chance = RuleI(NPC, BossCategoryChanceHell);

	std::map <int, uint8> pool;
	int pid = 0;
	pid += normal_chance;
	pool[pid] = LoC::MobNormal;
	pid += champion_chance;
	pool[pid] = LoC::MobChampion;
	pid += rare_chance;
	pool[pid] = LoC::MobRare;
	pid += unique_chance;
	pool[pid] = LoC::MobUnique;
	pid += super_unique_chance;
	pool[pid] = LoC::MobSuperUnique;
	pid += boss_chance;
	pool[pid] = LoC::MobBoss;

	int dice = zone->random.Int(1, pid);

	int lastPool = 0;
	for (auto entry = pool.begin(); entry != pool.end(); ++entry) {
		if (dice > entry->first) {
			lastPool = entry->first;
			continue;
		}
		return entry->second;
	}
	return 0; //normal by default
}

void NPC::AdjustStats(const NPCType* d, Spawn2 *in_respawn) {
	uint8 cat = GetCategory();

	if (cat > 0) npc_faction_id = 79; //make kos
	//Level was determined based on the spawngroup, we now adjust it's level with category
	int levelMod = 0;

	if (cat == LoC::MobChampion) levelMod = zone->random.Int(1, 5);
	if (cat == LoC::MobRare) levelMod = zone->random.Int(3, 8);
	if (cat == LoC::MobUnique) levelMod = zone->random.Int(5, 8);
	if (cat == LoC::MobSuperUnique) levelMod = zone->random.Int(9, 15);
	if (cat == LoC::MobBoss) levelMod = zone->random.Int(8, 20);
	SetLevel(level + levelMod);

	CalcNPCResists();
	CalcNPCRegen();
	
	

	if (zone->GetZoneID() < 200 || level < 48) {
		max_hp = level * 10;
		base_hp = level * 10;
	}
	else {
		max_hp = level * 50;
		base_hp = level * 50;
	}

	cur_hp = max_hp;
	min_dmg = 0;
	max_dmg = 0;
	CalcNPCDamage();

	if (cat == LoC::MobChampion) {
		min_damage *= 2;
		max_dmg *= 2;
		max_hp *= 2;
		size += 1.0f;
		SetLastName("Champion");
	}

	if (cat == LoC::MobRare) {
		min_damage *= 3;
		max_dmg *= 3;
		max_hp *= 3;
		size += 1.2f;
		SetLastName("Rare");
	}
	if (cat == LoC::MobUnique) {
		min_damage *= 3;
		max_dmg *= 3;
		max_hp *= 3;
		size += 1.8f;
		SetLastName("Unique");
	}
	if (cat == LoC::MobSuperUnique) {
		min_damage *= 3;
		max_dmg *= 3;
		max_hp *= 3;
		size += 2.1f;
		SetLastName("Super Unique");
	}
	if (cat == LoC::MobBoss) {
		min_damage *= 4;
		max_dmg *= 4;
		max_hp *= 5;
		size += 3.0f;
		SetLastName("Boss");
	}
	SetHP(GetMaxHP());
	npc_spells_id = 0;
	npc_spells_effects_id = 0;

	/*
	roambox_delay = zone->random.Int(1000, 10000);
	roambox_min_delay = zone->random.Int(500, 1000);
	roambox_distance = zone->random.Int(5, 50);
	*/
	
}

void NPC::AddAbilities(const NPCType* d, Spawn2 *in_respawn) {
	int cat = GetCategory();
	//Add any generic spawn abilities here
	if (cat == LoC::MobNormal) return;

	int prefixCount = 0;
	if (cat == LoC::MobChampion) prefixCount = zone->random.Int(0, 1);
	if (cat == LoC::MobRare) prefixCount = zone->random.Int(1, 2);
	if (cat == LoC::MobUnique) prefixCount = zone->random.Int(2, 3);
	if (cat == LoC::MobSuperUnique) prefixCount = zone->random.Int(2, 4);
	if (cat == LoC::MobBoss) prefixCount = zone->random.Int(2, 4);
	if (zone->GetInstanceID() == 1) prefixCount++;
	if (zone->GetInstanceID() == 2) prefixCount++;
	int prefixTotal = 0;
	int attempts = 0;
	SetSpecialAbility(UNMEZABLE, 1);
	SetSpecialAbility(UNCHARMABLE, 1);

	if (cat == LoC::MobSuperUnique || cat == LoC::MobBoss) {
		SetSpecialAbility(SPECATK_SUMMON, 1);		
		SetSpecialAbility(IMMUNE_FEIGN_DEATH, 1);
		SetSpecialAbility(SPECATK_RANGED_ATK, 1);
	}
	// This does not work as expected. Commenting out til it's implemented correctly.
	// Remove leading "a " from name
	//if (strncmp(GetName(), "a ", 2)) {
	//	strn0cpy(name, GetName() + 2, 64);
	//}

	while (prefixTotal < prefixCount) {
		int prefix = zone->random.Int(0, LoC::PrefixMax);
		if (AddPrefix((prefixTotal == prefixCount+1)?0:1, prefix)) prefixTotal++;
		attempts++;
		if (attempts > 100) break;
	}

	int suffixCount = 0;
	if (cat == LoC::MobChampion) suffixCount = zone->random.Int(0, 1);
	if (cat == LoC::MobRare) suffixCount = zone->random.Int(1, 2);
	if (cat == LoC::MobUnique) suffixCount = zone->random.Int(2, 3);
	if (cat == LoC::MobSuperUnique) suffixCount = zone->random.Int(2, 4);
	if (cat == LoC::MobBoss) suffixCount = zone->random.Int(2, 4);
	if (zone->GetInstanceID() == 1) suffixCount++;
	if (zone->GetInstanceID() == 2) suffixCount++;
	int suffixTotal = 0;
	attempts = 0;
	while (suffixTotal < prefixCount) {
		int suffix = zone->random.Int(0, LoC::SuffixMax);
		if (AddSuffix(suffix)) suffixTotal++;
		attempts++;
		if (attempts > 100) break;
	}
}


//DoItemization is called during NPC death
void NPC::DoItemization(Mob *killer) {
	int cat = GetCategory();	
	if (killer == NULL) return; //don't bother itemizing if we don't know who killer was

	int difficulty = zone->GetInstanceID();
	
	
	int drop_count = GetDropCount(killer);	
	if (drop_count == 0) {
		if (cat < 1) return;
		drop_count = 1;
	}
	AddCash(zone->random.Int(0, GetLevel()), zone->random.Int(0, GetLevel() / 2), zone->random.Int(0, GetLevel() / 3), zone->random.Int(0, GetLevel() / 4));
	if (cat > 0) AddCash(zone->random.Int(0, GetLevel()), zone->random.Int(0, GetLevel()), zone->random.Int(0, GetLevel() / 2), zone->random.Int(0, GetLevel() / 2));
	
	for (int i = 0; i < drop_count; i++) {
		uint32 item_id = 0;
		uint32 aug1_id = 0;
		uint32 aug2_id = 0;
		uint32 aug3_id = 0;
		uint32 aug4_id = 0;
		uint32 aug5_id = 0;
		uint32 aug6_id = 0;

		int rarity = GetItemRarity(killer);
		int slot_type = zone->random.Int(0, LoC::SlotMax);

		int class_type = 0;
		if (killer->GetClass() == SHADOWKNIGHT || killer->GetClass() == WARRIOR || killer->GetClass() == PALADIN) class_type = LoC::Tank;
		if (killer->GetClass() == BEASTLORD || killer->GetClass() == MONK || killer->GetClass() == BERSERKER||  killer->GetClass() == RANGER || killer->GetClass() == ROGUE || killer->GetClass() == BARD) class_type = LoC::Damage;
		if (killer->GetClass() == CLERIC || killer->GetClass() == DRUID || killer->GetClass() == SHAMAN) class_type = LoC::Support;
		if (killer->GetClass() == WIZARD || killer->GetClass() == NECROMANCER|| killer->GetClass() == MAGICIAN || killer->GetClass() == ENCHANTER) class_type = LoC::Caster;

		if (zone->random.Roll(RuleI(LoC, ItemizationOtherClassTypeChance))) {
			class_type = zone->random.Int(0, LoC::ClassMax);
		} 
		
		int item_level = GetItemLevel(killer);
		item_id = GetItemBase(rarity, slot_type, class_type, item_level);
		//aug1_id = GetItemProperty(1, rarity, slot_type, class_type, item_level);
		aug2_id = GetItemProperty(2, rarity, slot_type, class_type, item_level);
		aug3_id = GetItemProperty(3, rarity, slot_type, class_type, item_level);
		aug4_id = GetItemProperty(4, rarity, slot_type, class_type, item_level);
		aug5_id = GetItemProperty(5, rarity, slot_type, class_type, item_level);
		aug6_id = GetItemProperty(6, rarity, slot_type, class_type, item_level);

		if (item_id == 0) return;
		AddItem(item_id, 1, false, aug1_id, aug2_id, aug3_id, aug4_id, aug5_id, aug6_id);
	}
	return;
}



int NPC::GetItemLevel(Mob *killer) {
	int item_level = 0;
	item_level = GetLevel(); 
	return item_level;
}


//GetDropCount determines how many drops a mob dropped, and is called by DoItemization
int NPC::GetDropCount(Mob *killer) {
	int difficulty = zone->GetInstanceID();
	int cat = GetCategory();
	//Drop count is weighted to lean towards less more than more.
	int drop_0_chance = 0;
	if (difficulty == LoC::Normal) drop_0_chance = RuleI(NPC, ItemDrop0ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_0_chance = RuleI(NPC, ItemDrop0ChanceNightmare);
	if (difficulty == LoC::Hell) drop_0_chance = RuleI(NPC, ItemDrop0ChanceHell);
	if (cat >= LoC::MobChampion) drop_0_chance = 0;
	int drop_1_chance = 0;
	if (difficulty == LoC::Normal) drop_1_chance = RuleI(NPC, ItemDrop1ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_1_chance = RuleI(NPC, ItemDrop1ChanceNightmare);
	if (difficulty == LoC::Hell) drop_1_chance = RuleI(NPC, ItemDrop1ChanceHell);
	int drop_2_chance = 0;
	if (cat >= LoC::MobRare) drop_0_chance = 0;
	if (difficulty == LoC::Normal) drop_2_chance = RuleI(NPC, ItemDrop2ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_2_chance = RuleI(NPC, ItemDrop2ChanceNightmare);
	if (difficulty == LoC::Hell) drop_2_chance = RuleI(NPC, ItemDrop2ChanceHell);
	int drop_3_chance = 0;
	if (cat >= LoC::MobUnique) drop_0_chance = 0;
	if (difficulty == LoC::Normal) drop_3_chance = RuleI(NPC, ItemDrop3ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_3_chance = RuleI(NPC, ItemDrop3ChanceNightmare);
	if (difficulty == LoC::Hell) drop_3_chance = RuleI(NPC, ItemDrop3ChanceHell);
	if (cat >= LoC::MobSuperUnique) drop_0_chance = 0;
	int drop_4_chance = 0;
	if (difficulty == LoC::Normal) drop_4_chance = RuleI(NPC, ItemDrop4ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_4_chance = RuleI(NPC, ItemDrop4ChanceNightmare);
	if (difficulty == LoC::Hell) drop_4_chance = RuleI(NPC, ItemDrop4ChanceHell);
	int drop_5_chance = 0;
	if (difficulty == LoC::Normal) drop_5_chance = RuleI(NPC, ItemDrop5ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_5_chance = RuleI(NPC, ItemDrop5ChanceNightmare);
	if (difficulty == LoC::Hell) drop_5_chance = RuleI(NPC, ItemDrop5ChanceHell);
	int drop_6_chance = 0;
	if (difficulty == LoC::Normal) drop_6_chance = RuleI(NPC, ItemDrop6ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_6_chance = RuleI(NPC, ItemDrop6ChanceNightmare);
	if (difficulty == LoC::Hell) drop_6_chance = RuleI(NPC, ItemDrop6ChanceHell);

	//Tally up chances
	std::map <int, uint8> pool;
	int pid = 0;
	if (drop_0_chance > 0) {
		pid += drop_0_chance;
		pool[pid] = 0;
	}
	if (drop_1_chance > 0) {
		pid += drop_1_chance;
		pool[pid] = 1;
	}
	if (drop_2_chance > 0) {
		pid += drop_2_chance;
		pool[pid] = 2;
	}
	if (drop_3_chance > 0) {
		pid += drop_3_chance;
		pool[pid] = 3;
	}
	if (drop_4_chance > 0) {
		pid += drop_4_chance;
		pool[pid] = 4;
	}
	if (drop_5_chance > 0) {
		pid += drop_5_chance;
		pool[pid] = 5;
	}
	if (drop_6_chance > 0) {
		pid += drop_6_chance;
		pool[pid] = 6;
	}

	int dice = zone->random.Int(1, pid);

	int lastPool = 0;
	for (auto entry = pool.begin(); entry != pool.end(); ++entry) {
		if (dice > entry->first) {
			lastPool = entry->first;
			continue;
		}
		return entry->second;
	}
	return 0; //none by default
}

//GetItemRarity is called by DoItemization, and is determined during a mob's death for magic calculations
int NPC::GetItemRarity(Mob *killer) {
	int difficulty = zone->GetInstanceID();
	int cat = GetCategory();

	int common_chance = 0;
	if (difficulty == LoC::Normal) common_chance = RuleI(NPC, ItemCommonChanceNormal);
	if (difficulty == LoC::Nightmare) common_chance = RuleI(NPC, ItemCommonChanceNightmare);
	if (difficulty == LoC::Hell) common_chance = RuleI(NPC, ItemCommonChanceHell);
	if (cat >= LoC::MobChampion) common_chance = 0;
	int uncommon_chance = 0;
	if (difficulty == LoC::Normal) uncommon_chance = RuleI(NPC, ItemUncommonChanceNormal);
	if (difficulty == LoC::Nightmare) uncommon_chance = RuleI(NPC, ItemUncommonChanceNightmare);
	if (difficulty == LoC::Hell) uncommon_chance = RuleI(NPC, ItemUncommonChanceHell);
	int rare_chance = 0;
	if (difficulty == LoC::Normal) rare_chance = RuleI(NPC, ItemRareChanceNormal);
	if (difficulty == LoC::Nightmare) rare_chance = RuleI(NPC, ItemRareChanceNightmare);
	if (difficulty == LoC::Hell) rare_chance = RuleI(NPC, ItemRareChanceHell);
	int legendary_chance = 0;
	if (difficulty == LoC::Normal) legendary_chance = RuleI(NPC, ItemLegendaryChanceNormal);
	if (difficulty == LoC::Nightmare) legendary_chance = RuleI(NPC, ItemLegendaryChanceNightmare);
	if (difficulty == LoC::Hell) legendary_chance = RuleI(NPC, ItemLegendaryChanceHell);
	int unique_chance = 0;
	if (difficulty == LoC::Normal) unique_chance = RuleI(NPC, ItemUniqueChanceNormal);
	if (difficulty == LoC::Nightmare) unique_chance = RuleI(NPC, ItemUniqueChanceNightmare);
	if (difficulty == LoC::Hell) unique_chance = RuleI(NPC, ItemUniqueChanceHell);

	//Add Magic Find modifiers here. Killer is referenced as an argument, 
	//so we can iterate group to calculate magic find bonuses, boosting non-common items.
	int cha = killer->GetCHA();
	if (cha > 255) cha = 255;
	if (cha > 0) {
		int mfMod = 3;
		if (difficulty == LoC::Normal) mfMod = 3;
		if (difficulty == LoC::Nightmare) mfMod = 2;			
		if (difficulty == LoC::Hell) mfMod = 1;
		uncommon_chance += cha / mfMod;
		rare_chance += cha / mfMod;
		legendary_chance += cha / mfMod;
		unique_chance += cha / mfMod;
	}

	//Tally up chances
	std::map <int, uint8> pool;
	int pid = 0;
	pid += common_chance;
	pool[pid] = LoC::Common;
	pid += uncommon_chance;
	pool[pid] = LoC::Uncommon;
	pid += rare_chance;
	pool[pid] = LoC::Rare;
	pid += legendary_chance;
	pool[pid] = LoC::Legendary;
	pid += unique_chance;
	pool[pid] = LoC::Unique;

	int dice = zone->random.Int(1, pid);

	int lastPool = 0;
	for (auto entry = pool.begin(); entry != pool.end(); ++entry) {
		if (dice > entry->first) {
			lastPool = entry->first;
			continue;
		}
		return entry->second;
	}
	return LoC::Common; //common by default
}

int32 NPC::AdjustExperience(int base_exp, Mob *killer) {
	int exp = base_exp;
	int cat = GetCategory();
	if (cat == 0) return exp;

	if (cat == LoC::MobChampion) exp *= 4;
	if (cat == LoC::MobRare) exp *= 4;
	if (cat == LoC::MobUnique) exp *= 5;
	if (cat == LoC::MobSuperUnique) exp *= 6;
	if (cat == LoC::MobBoss) exp *= 5;
	return exp;
}

void NPC::SpawnMinions(const NPCType *d) {
	return;
	int cat = GetCategory();
	if (cat == 0) return;
	int count = 0;
	if (cat == LoC::MobChampion) count = 2;
	if (cat == LoC::MobRare) count = 3;
	if (cat == LoC::MobUnique) count = 4;
	if (cat == LoC::MobSuperUnique) count = 5;
	if (cat == LoC::MobBoss) count = 1;
	for (int i = 0; i < count; i++) {
		auto pos = GetPosition();
		pos.x += zone->random.Int(-10, 10);
		pos.y += zone->random.Int(-10, 10);
		NPC* npc = new NPC(d, nullptr, pos, FlyMode3);
		npc->npc_spells_id = 0;
		npc->npc_spells_effects_id = 0;
		npc->SetOwnerID(GetID());		
		npc->max_hp = max_hp / 2;
		npc->SetHP(GetMaxHP());
		npc->min_dmg = min_dmg / 2;
		npc->max_dmg = max_dmg / 2;
		npc->SetFollowID(GetID());
		npc->SetFollowDistance(zone->random.Int(10, 50));
		npc->SetLastName("Minion");
		npc->roambox_delay = zone->random.Int(1000, 10000);
		npc->roambox_min_delay = zone->random.Int(500, 1000);
		npc->roambox_distance = zone->random.Int(5, 50);
		npc->size -= zone->random.Real(0, 1);
		entity_list.AddNPC(npc);
	}

}

bool NPC::AddPrefix(int counter, int prefix) {
	if (strlen(name) > 20) return false;
	if (prefix == LoC::PrefixGloom) {
		SetName(StringFormat("%sloom_%s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 167; //black reaver
	}
	if (prefix == LoC::PrefixGray) {
		SetName(StringFormat("%sray_%s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1401;
	}
	if (prefix == LoC::PrefixDire) {
		SetName(StringFormat("%sire_%s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1402;
	}
	if (prefix == LoC::PrefixBlack) {
		SetName(StringFormat("%slack_%s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1403;
	}
	if (prefix == LoC::PrefixShadow) {
		SetName(StringFormat("%shadow_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1404;
	}
	if (prefix == LoC::PrefixHaze) {
		SetName(StringFormat("%saze_%s", (counter == 0) ? "H" : "h", name).c_str());
		npc_spells_id = 1405;
	}
	if (prefix == LoC::PrefixWind) {
		SetName(StringFormat("%sind_%s", (counter == 0) ? "W" : "w", name).c_str());
		npc_spells_id = 1406;
	}
	if (prefix == LoC::PrefixStorm) {
		SetName(StringFormat("%storm_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1407;
	}
	if (prefix == LoC::PrefixWarp) {
		SetName(StringFormat("%sarp_%s", (counter == 0) ? "W" : "w", name).c_str());
		npc_spells_id = 1408;
	}
	if (prefix == LoC::PrefixNight) {
		SetName(StringFormat("%sight_%s", (counter == 0) ? "N" : "n", name).c_str());
		npc_spells_id = 1409;
	}
	if (prefix == LoC::PrefixMoon) {
		SetName(StringFormat("%soon_%s", (counter == 0) ? "M" : "m", name).c_str());
		npc_spells_id = 1410;
	}
	if (prefix == LoC::PrefixStar) {
		SetName(StringFormat("%star_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1411;
	}
	if (prefix == LoC::PrefixPit) {
		SetName(StringFormat("%sit_%s", (counter == 0) ? "P" : "p", name).c_str());
		npc_spells_id = 1412;
	}
	if (prefix == LoC::PrefixFire) {
		SetName(StringFormat("%sire_%s", (counter == 0) ? "F" : "f", name).c_str());
		npc_spells_id = 1413;
	}
	if (prefix == LoC::PrefixCold) {
		SetName(StringFormat("%sold_%s", (counter == 0) ? "C" : "c", name).c_str());
		npc_spells_id = 1414;
	}
	if (prefix == LoC::PrefixSeethe) {
		SetName(StringFormat("%seethe_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1415;
	}
	if (prefix == LoC::PrefixSharp) {
		SetName(StringFormat("%sharp_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1416;
	}
	if (prefix == LoC::PrefixAsh) {
		SetName(StringFormat("%ssh_%s", (counter == 0) ? "A" : "a", name).c_str());
		npc_spells_id = 1417;
	}
	if (prefix == LoC::PrefixBlade) {
		SetName(StringFormat("%slade_%s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1418;
	}
	if (prefix == LoC::PrefixSteel) {
		SetName(StringFormat("%steel_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1419;
	}
	if (prefix == LoC::PrefixStone) {
		SetName(StringFormat("%stone_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1420;
	}
	if (prefix == LoC::PrefixRust) {
		SetName(StringFormat("%sust_%s", (counter == 0) ? "R" : "r", name).c_str());
		npc_spells_id = 1421;
	}
	if (prefix == LoC::PrefixMold) {
		SetName(StringFormat("%sold_%s", (counter == 0) ? "M" : "m", name).c_str());
		npc_spells_id = 1422;
	}
	if (prefix == LoC::PrefixBlight) {
		SetName(StringFormat("%slight_%s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1423;
	}
	if (prefix == LoC::PrefixPlague) {
		SetName(StringFormat("%slague_%s", (counter == 0) ? "P" : "p", name).c_str());
		npc_spells_id = 1424;
	}
	if (prefix == LoC::PrefixRot) {
		SetName(StringFormat("%sot_%s", (counter == 0) ? "R" : "r", name).c_str());
		npc_spells_id = 1425;
	}
	if (prefix == LoC::PrefixOoze) {
		SetName(StringFormat("%soze_%s", (counter == 0) ? "O" : "o", name).c_str());
		npc_spells_id = 1426;
	}
	if (prefix == LoC::PrefixPuke) {
		SetName(StringFormat("%suke_%s", (counter == 0) ? "P" : "p", name).c_str());
		npc_spells_id = 1427;
	}
	if (prefix == LoC::PrefixSnot) {
		SetName(StringFormat("%snot_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1428;
	}
	if (prefix == LoC::PrefixBile) {
		SetName(StringFormat("%sile_%s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1429;
	}
	if (prefix == LoC::PrefixBlood) {
		SetName(StringFormat("%slood_%s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1430;
	}
	if (prefix == LoC::PrefixPulse) {
		SetName(StringFormat("%sulse_%s", (counter == 0) ? "P" : "p", name).c_str());
		npc_spells_id = 1431;
	}
	if (prefix == LoC::PrefixGut) {
		SetName(StringFormat("%sut_%s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1432;
	}
	if (prefix == LoC::PrefixGore) {
		SetName(StringFormat("%sore_%s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1433;
	}
	if (prefix == LoC::PrefixFlesh) {
		SetName(StringFormat("%slesh_%s", (counter == 0) ? "F" : "f", name).c_str());
		npc_spells_id = 1434;
	}
	if (prefix == LoC::PrefixBone) {
		SetName(StringFormat("%sone_%s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1435;
	}
	if (prefix == LoC::PrefixSpine) {
		SetName(StringFormat("%spine_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1436;
	}
	if (prefix == LoC::PrefixMind) {
		SetName(StringFormat("%sind_%s", (counter == 0) ? "M" : "m", name).c_str());
		npc_spells_id = 1437;
	}
	if (prefix == LoC::PrefixSpirit) {
		SetName(StringFormat("%spirit_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1438;
	}
	if (prefix == LoC::PrefixSoul) {
		SetName(StringFormat("%soul_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1439;
	}
	if (prefix == LoC::PrefixWrath) {
		SetName(StringFormat("%srath_%s", (counter == 0) ? "W" : "w", name).c_str());
		npc_spells_id = 1440;
	}
	if (prefix == LoC::PrefixGrief) {
		SetName(StringFormat("%srief_%s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1441;
	}
	if (prefix == LoC::PrefixFoul) {
		SetName(StringFormat("%soul_%s", (counter == 0) ? "F" : "f", name).c_str());
		npc_spells_id = 1442;
	}
	if (prefix == LoC::PrefixVile) {
		SetName(StringFormat("%sile_%s", (counter == 0) ? "V" : "v", name).c_str());
		npc_spells_id = 1443;
	}
	if (prefix == LoC::PrefixSin) {
		SetName(StringFormat("%sin_%s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1444;
	}
	if (prefix == LoC::PrefixChaos) {
		SetName(StringFormat("%shaos_%s", (counter == 0) ? "C" : "c", name).c_str());
		npc_spells_id = 1445;
	}
	if (prefix == LoC::PrefixDread) {
		SetName(StringFormat("%sread_%s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1446;
	}
	if (prefix == LoC::PrefixDoom) {
		SetName(StringFormat("%soom_%s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1447;
	}
	if (prefix == LoC::PrefixBane) {
		SetName(StringFormat("%sane_%s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1448;
	}
	if (prefix == LoC::PrefixDeath) {
		SetName(StringFormat("%seath_%s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1449;
	}
	if (prefix == LoC::PrefixViper) {
		SetName(StringFormat("%siper_%s", (counter == 0) ? "V" : "v", name).c_str());
		npc_spells_id = 1450;
	}
	if (prefix == LoC::PrefixDragon) {
		SetName(StringFormat("%sragon_%s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1451;
	}
	if (prefix == LoC::PrefixDevil) {
		SetName(StringFormat("%sevil_%s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1452;
	}
	return true;
}

bool NPC::AddSuffix(int suffix) {
	if (strlen(name) > 20) return false;
	if (suffix == LoC::SuffixTouch) {
		SetName(StringFormat("%s_Touch", name).c_str());
		npc_spells_effects_id = 20;
	}
	if (suffix == LoC::SuffixSpell) {
		SetName(StringFormat("%s_Spell", name).c_str());
		npc_spells_effects_id = 7;
	}
	if (suffix == LoC::SuffixFeast) {
		SetName(StringFormat("%s_Feast", name).c_str());
		npc_spells_effects_id = 16;
	}
	if (suffix == LoC::SuffixWound) {
		SetName(StringFormat("%s_Wound", name).c_str());
		npc_spells_effects_id = 4;
	}
	if (suffix == LoC::SuffixGrin) {
		SetName(StringFormat("%s_Grin", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMaim) {
		SetName(StringFormat("%s_Maim", name).c_str());
		npc_spells_effects_id = 3;
	}
	if (suffix == LoC::SuffixHack) {
		SetName(StringFormat("%s_Hack", name).c_str());
		SetSpecialAbility(SPECATK_RAMPAGE, 1);
	}
	if (suffix == LoC::SuffixBite) {
		SetName(StringFormat("%s_Bite", name).c_str());
		npc_spells_effects_id = 17;
	}
	if (suffix == LoC::SuffixRend) {
		SetName(StringFormat("%s_Rend", name).c_str());
		npc_spells_effects_id = 17;
	}
	if (suffix == LoC::SuffixBurn) {
		SetName(StringFormat("%s_Burn", name).c_str());
		npc_spells_id = 17;
	}
	if (suffix == LoC::SuffixRip) {
		SetName(StringFormat("%s_Rip", name).c_str());
		npc_spells_id = 36;
	}
	if (suffix == LoC::SuffixKill) {
		SetName(StringFormat("%s_Kill", name).c_str());
		npc_spells_id = 76;
	}
	if (suffix == LoC::SuffixCall) {
		SetName(StringFormat("%s_Call", name).c_str());
		SetSpecialAbility(SPECATK_SUMMON, 1);
	}
	if (suffix == LoC::SuffixVex) {
		SetName(StringFormat("%s_Vex", name).c_str());
		npc_spells_id = 134;
	}
	if (suffix == LoC::SuffixJade) {
		SetName(StringFormat("%s_Jade", name).c_str());
		npc_spells_id = 154;
	}
	if (suffix == LoC::SuffixWeb) {
		SetName(StringFormat("%s_Web", name).c_str());
		npc_spells_id = 158;
	}
	if (suffix == LoC::SuffixShield) {
		SetName(StringFormat("%s_Shield", name).c_str());
		npc_spells_effects_id = 11;
	}
	if (suffix == LoC::SuffixKiller) {
		SetName(StringFormat("%s_Killer", name).c_str());
		npc_spells_id = 388;
	}
	if (suffix == LoC::SuffixRazor) {
		SetName(StringFormat("%s_Razor", name).c_str());
		npc_spells_id = 404;
	}
	if (suffix == LoC::SuffixDrinker) {
		SetName(StringFormat("%s_Drinker", name).c_str());
		npc_spells_id = 442;
	}
	if (suffix == LoC::SuffixShifter) {
		SetName(StringFormat("%s_Shifter", name).c_str());
		SetSpecialAbility(SPECATK_SUMMON, 1);
	}
	if (suffix == LoC::SuffixCrawler) {
		SetName(StringFormat("%s_Crawler", name).c_str());
		npc_spells_id = 16;		
	}
	if (suffix == LoC::SuffixDancer) {
		SetName(StringFormat("%s_Dancer", name).c_str());
		npc_spells_id = 11;
	}
	if (suffix == LoC::SuffixBender) {
		SetName(StringFormat("%s_Bender", name).c_str());
		npc_spells_id = 29;
	}
	if (suffix == LoC::SuffixWeaver) {
		SetName(StringFormat("%s_Weaver", name).c_str());
		npc_spells_id = 35;
	}
	if (suffix == LoC::SuffixEater) {
		SetName(StringFormat("%s_Eater", name).c_str());
		npc_spells_id = 59;
	}
	if (suffix == LoC::SuffixWidow) {
		SetName(StringFormat("%s_Widow", name).c_str());
		
	}
	if (suffix == LoC::SuffixMaggot) {
		SetName(StringFormat("%s_Maggot", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSpawn) {
		SetName(StringFormat("%s_Spawn", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWight) {
		SetName(StringFormat("%s_Wight", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixGrumble) {
		SetName(StringFormat("%s_Grumble", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixGrowler) {
		SetName(StringFormat("%s_Growler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSnarl) {
		SetName(StringFormat("%s_Snarl", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWolf) {
		SetName(StringFormat("%s_Wolf", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCrow) {
		SetName(StringFormat("%s_Crow", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHawk) {
		SetName(StringFormat("%s_Hawk", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCloud) {
		SetName(StringFormat("%s_Cloud", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBang) {
		SetName(StringFormat("%s_Bang", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHead) {
		SetName(StringFormat("%s_Head", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSkull) {
		SetName(StringFormat("%s_Skull", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBrow) {
		SetName(StringFormat("%s_Brow", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixEye) {
		SetName(StringFormat("%s_Eye", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMaw) {
		SetName(StringFormat("%s_Maw", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixTongue) {
		SetName(StringFormat("%s_Tongue", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFang) {
		SetName(StringFormat("%s_Fang", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHorn) {
		SetName(StringFormat("%s_Horn", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixThorn) {
		SetName(StringFormat("%s_Thorn", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixClaw) {
		SetName(StringFormat("%s_Claw", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFist) {
		SetName(StringFormat("%s_Fist", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHeart) {
		SetName(StringFormat("%s_Heart", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixShank) {
		SetName(StringFormat("%s_Shank", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSkin) {
		SetName(StringFormat("%s_Skin", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWing) {
		SetName(StringFormat("%s_Wing", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixPox) {
		SetName(StringFormat("%s_Pox", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFester) {
		SetName(StringFormat("%s_Fester", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBlister) {
		SetName(StringFormat("%s_Blister", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixPus) {
		SetName(StringFormat("%s_Pus", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSlime) {
		SetName(StringFormat("%s_Slime", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDrool) {
		SetName(StringFormat("%s_Drool", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFroth) {
		SetName(StringFormat("%s_Froth", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSludge) {
		SetName(StringFormat("%s_Sludge", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixVenom) {
		SetName(StringFormat("%s_Venom", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixPoison) {
		SetName(StringFormat("%s_Poison", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBreak) {
		SetName(StringFormat("%s_Break", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixShard) {
		SetName(StringFormat("%s_Shard", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFlame) {
		SetName(StringFormat("%s_Flame", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMaul) {
		SetName(StringFormat("%s_Maul", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixThirst) {
		SetName(StringFormat("%s_Thirst", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixLust) {
		SetName(StringFormat("%s_Lust", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHammer) {
		SetName(StringFormat("%s_the Hammer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixAxe) {
		SetName(StringFormat("%s_the Axe", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSharp) {
		SetName(StringFormat("%s_the Sharp", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixJagged) {
		SetName(StringFormat("%s_the Jagged", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFlayer) {
		SetName(StringFormat("%s_the Flayer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSlasher) {
		SetName(StringFormat("%s_the Slasher", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixImpaler) {
		SetName(StringFormat("%s_the Impaler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHunter) {
		SetName(StringFormat("%s_the Hunter", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSlayer) {
		SetName(StringFormat("%s_the Slayer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMauler) {
		SetName(StringFormat("%s_the Mauler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDestroyer) {
		SetName(StringFormat("%s_the Destroyer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixQuick) {
		SetName(StringFormat("%s_the Quick", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWitch) {
		SetName(StringFormat("%s_the Witch", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMad) {
		SetName(StringFormat("%s_the Mad", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWraith) {
		SetName(StringFormat("%s_the Wraith", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixShade) {
		SetName(StringFormat("%s_the Shade", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDead) {
		SetName(StringFormat("%s_the Dead", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixUnholy) {
		SetName(StringFormat("%s_the Unholy", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHowler) {
		SetName(StringFormat("%s_the Howler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixGrim) {
		SetName(StringFormat("%s_the Grim", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDark) {
		SetName(StringFormat("%s_the Dark", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixTainted) {
		SetName(StringFormat("%s_the Tainted", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixUnclean) {
		SetName(StringFormat("%s_the Unclean", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHungry) {
		SetName(StringFormat("%s_the Hungry", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCold) {
		SetName(StringFormat("%s_the Cold", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixAuraEnchanted) {
		SetName(StringFormat("%s_Aura Enchanted", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixColdEnchanted) {
		SetName(StringFormat("%s_Cold Enchanted", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCursed) {
		SetName(StringFormat("%s_Cursed", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixExtraFast) {
		SetName(StringFormat("%s_Extra Fast", name).c_str());
		npc_spells_effects_id = 30;
	}
	if (suffix == LoC::SuffixExtraStrong) {
		SetName(StringFormat("%s_Extra Strong", name).c_str());				
		min_damage *= 3;
	}
	if (suffix == LoC::SuffixFireEnchanted) {
		SetName(StringFormat("%s_Fire Enchanted", name).c_str());
		FR *= 10;
		npc_spells_id = 18;
	}
	if (suffix == LoC::SuffixLightningEnchanted) {
		SetName(StringFormat("%s_Disease Enchanted", name).c_str());
		DR *= 10;
	}
	if (suffix == LoC::SuffixMagicResistant) {
		SetName(StringFormat("%s_Magic Resistant", name).c_str());
		MR *= 10;
	}
	if (suffix == LoC::SuffixManaBurn) {
		SetName(StringFormat("%s_Mana Burn", name).c_str());
		npc_spells_id = 159;
	}
	if (suffix == LoC::SuffixMultiShot) {
		SetName(StringFormat("%s_Multi-Shot", name).c_str());
		npc_spells_id = 134;
	}
	if (suffix == LoC::SuffixSpectralHit) {
		SetName(StringFormat("%s_Spectral Hit", name).c_str());
		npc_spells_id = 114;
	}
	if (suffix == LoC::SuffixStoneSkin) {
		SetName(StringFormat("%s_Stone Skin", name).c_str());
		AC *= 4;
	}
	if (suffix == LoC::SuffixTeleporting) {
		SetName(StringFormat("%s_Teleporting", name).c_str());
		npc_spells_id = 106;
	}
	return true;
}

//GetItemBase is called during Itemization, and is autogenerated from the sheets
int NPC::GetItemBase(int rarity, int slot_type, int class_type, int item_level) {
	int item_id = 0;

	item_level = ((item_level + 10 / 2) / 10) * 10; //round to nearest 10th
	if (item_level < 10) item_level = 10;

	static Item_Base items[] = {
		/* AUTOGENERATED VIA SHEETS*/
		Item_Base(1000, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 10),
		Item_Base(1001, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 10),
		Item_Base(1002, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 10),
		Item_Base(1003, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 10),
		Item_Base(1004, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 10),
		Item_Base(1005, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 10),
		Item_Base(1006, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 10),
		Item_Base(1007, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 10),
		Item_Base(1008, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 10),
		Item_Base(1009, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 10),
		Item_Base(1010, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 10),
		Item_Base(1011, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 10),
		Item_Base(1012, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 10),
		Item_Base(1013, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 10),
		Item_Base(1014, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 10),
		Item_Base(1015, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 10),
		Item_Base(1016, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 10),
		Item_Base(1017, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 10),
		Item_Base(1018, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 10),
		Item_Base(1019, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 10),
		Item_Base(1020, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 10),
		Item_Base(1021, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 10),
		Item_Base(1022, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 10),
		Item_Base(1023, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 10),
		Item_Base(1024, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 10),
		Item_Base(1025, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 20),
		Item_Base(1026, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 20),
		Item_Base(1027, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 20),
		Item_Base(1028, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 20),
		Item_Base(1029, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 20),
		Item_Base(1030, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 20),
		Item_Base(1031, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 20),
		Item_Base(1032, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 20),
		Item_Base(1033, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 20),
		Item_Base(1034, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 20),
		Item_Base(1035, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 20),
		Item_Base(1036, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 20),
		Item_Base(1037, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 20),
		Item_Base(1038, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 20),
		Item_Base(1039, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 20),
		Item_Base(1040, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 20),
		Item_Base(1041, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 20),
		Item_Base(1042, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 20),
		Item_Base(1043, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 20),
		Item_Base(1044, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 20),
		Item_Base(1045, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 20),
		Item_Base(1046, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 20),
		Item_Base(1047, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 20),
		Item_Base(1048, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 20),
		Item_Base(1049, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 20),
		Item_Base(1050, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 30),
		Item_Base(1051, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 30),
		Item_Base(1052, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 30),
		Item_Base(1053, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 30),
		Item_Base(1054, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 30),
		Item_Base(1055, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 30),
		Item_Base(1056, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 30),
		Item_Base(1057, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 30),
		Item_Base(1058, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 30),
		Item_Base(1059, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 30),
		Item_Base(1060, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 30),
		Item_Base(1061, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 30),
		Item_Base(1062, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 30),
		Item_Base(1063, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 30),
		Item_Base(1064, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 30),
		Item_Base(1065, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 30),
		Item_Base(1066, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 30),
		Item_Base(1067, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 30),
		Item_Base(1068, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 30),
		Item_Base(1069, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 30),
		Item_Base(1070, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 30),
		Item_Base(1071, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 30),
		Item_Base(1072, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 30),
		Item_Base(1073, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 30),
		Item_Base(1074, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 30),
		Item_Base(1075, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 40),
		Item_Base(1076, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 40),
		Item_Base(1077, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 40),
		Item_Base(1078, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 40),
		Item_Base(1079, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 40),
		Item_Base(1080, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 40),
		Item_Base(1081, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 40),
		Item_Base(1082, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 40),
		Item_Base(1083, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 40),
		Item_Base(1084, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 40),
		Item_Base(1085, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 40),
		Item_Base(1086, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 40),
		Item_Base(1087, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 40),
		Item_Base(1088, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 40),
		Item_Base(1089, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 40),
		Item_Base(1090, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 40),
		Item_Base(1091, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 40),
		Item_Base(1092, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 40),
		Item_Base(1093, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 40),
		Item_Base(1094, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 40),
		Item_Base(1095, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 40),
		Item_Base(1096, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 40),
		Item_Base(1097, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 40),
		Item_Base(1098, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 40),
		Item_Base(1099, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 40),
		Item_Base(1100, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 50),
		Item_Base(1101, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 50),
		Item_Base(1102, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 50),
		Item_Base(1103, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 50),
		Item_Base(1104, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 50),
		Item_Base(1105, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 50),
		Item_Base(1106, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 50),
		Item_Base(1107, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 50),
		Item_Base(1108, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 50),
		Item_Base(1109, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 50),
		Item_Base(1110, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 50),
		Item_Base(1111, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 50),
		Item_Base(1112, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 50),
		Item_Base(1113, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 50),
		Item_Base(1114, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 50),
		Item_Base(1115, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 50),
		Item_Base(1116, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 50),
		Item_Base(1117, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 50),
		Item_Base(1118, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 50),
		Item_Base(1119, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 50),
		Item_Base(1120, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 50),
		Item_Base(1121, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 50),
		Item_Base(1122, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 50),
		Item_Base(1123, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 50),
		Item_Base(1124, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 50),
		Item_Base(1125, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 60),
		Item_Base(1126, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 60),
		Item_Base(1127, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 60),
		Item_Base(1128, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 60),
		Item_Base(1129, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 60),
		Item_Base(1130, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 60),
		Item_Base(1131, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 60),
		Item_Base(1132, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 60),
		Item_Base(1133, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 60),
		Item_Base(1134, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 60),
		Item_Base(1135, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 60),
		Item_Base(1136, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 60),
		Item_Base(1137, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 60),
		Item_Base(1138, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 60),
		Item_Base(1139, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 60),
		Item_Base(1140, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 60),
		Item_Base(1141, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 60),
		Item_Base(1142, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 60),
		Item_Base(1143, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 60),
		Item_Base(1144, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 60),
		Item_Base(1145, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 60),
		Item_Base(1146, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 60),
		Item_Base(1147, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 60),
		Item_Base(1148, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 60),
		Item_Base(1149, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 60),
		Item_Base(1150, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 70),
		Item_Base(1151, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 70),
		Item_Base(1152, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 70),
		Item_Base(1153, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 70),
		Item_Base(1154, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 70),
		Item_Base(1155, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 70),
		Item_Base(1156, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 70),
		Item_Base(1157, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 70),
		Item_Base(1158, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 70),
		Item_Base(1159, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 70),
		Item_Base(1160, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 70),
		Item_Base(1161, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 70),
		Item_Base(1162, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 70),
		Item_Base(1163, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 70),
		Item_Base(1164, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 70),
		Item_Base(1165, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 70),
		Item_Base(1166, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 70),
		Item_Base(1167, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 70),
		Item_Base(1168, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 70),
		Item_Base(1169, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 70),
		Item_Base(1170, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 70),
		Item_Base(1171, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 70),
		Item_Base(1172, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 70),
		Item_Base(1173, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 70),
		Item_Base(1174, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 70),
		Item_Base(1175, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 80),
		Item_Base(1176, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 80),
		Item_Base(1177, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 80),
		Item_Base(1178, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 80),
		Item_Base(1179, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 80),
		Item_Base(1180, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 80),
		Item_Base(1181, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 80),
		Item_Base(1182, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 80),
		Item_Base(1183, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 80),
		Item_Base(1184, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 80),
		Item_Base(1185, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 80),
		Item_Base(1186, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 80),
		Item_Base(1187, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 80),
		Item_Base(1188, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 80),
		Item_Base(1189, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 80),
		Item_Base(1190, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 80),
		Item_Base(1191, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 80),
		Item_Base(1192, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 80),
		Item_Base(1193, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 80),
		Item_Base(1194, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 80),
		Item_Base(1195, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 80),
		Item_Base(1196, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 80),
		Item_Base(1197, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 80),
		Item_Base(1198, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 80),
		Item_Base(1199, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 80),
		Item_Base(1200, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 90),
		Item_Base(1201, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 90),
		Item_Base(1202, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 90),
		Item_Base(1203, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 90),
		Item_Base(1204, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 90),
		Item_Base(1205, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 90),
		Item_Base(1206, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 90),
		Item_Base(1207, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 90),
		Item_Base(1208, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 90),
		Item_Base(1209, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 90),
		Item_Base(1210, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 90),
		Item_Base(1211, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 90),
		Item_Base(1212, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 90),
		Item_Base(1213, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 90),
		Item_Base(1214, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 90),
		Item_Base(1215, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 90),
		Item_Base(1216, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 90),
		Item_Base(1217, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 90),
		Item_Base(1218, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 90),
		Item_Base(1219, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 90),
		Item_Base(1220, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 90),
		Item_Base(1221, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 90),
		Item_Base(1222, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 90),
		Item_Base(1223, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 90),
		Item_Base(1224, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 90),
		Item_Base(1225, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 100),
		Item_Base(1226, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 100),
		Item_Base(1227, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 100),
		Item_Base(1228, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 100),
		Item_Base(1229, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 100),
		Item_Base(1230, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 100),
		Item_Base(1231, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 100),
		Item_Base(1232, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 100),
		Item_Base(1233, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 100),
		Item_Base(1234, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 100),
		Item_Base(1235, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 100),
		Item_Base(1236, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 100),
		Item_Base(1237, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 100),
		Item_Base(1238, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 100),
		Item_Base(1239, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 100),
		Item_Base(1240, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 100),
		Item_Base(1241, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 100),
		Item_Base(1242, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 100),
		Item_Base(1243, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 100),
		Item_Base(1244, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 100),
		Item_Base(1245, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 100),
		Item_Base(1246, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 100),
		Item_Base(1247, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 100),
		Item_Base(1248, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 100),
		Item_Base(1249, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 100),
		Item_Base(1250, LoC::Common, LoC::Tank, LoC::SlotEar, 21, 110),
		Item_Base(1251, LoC::Common, LoC::Tank, LoC::SlotNeck, 21, 110),
		Item_Base(1252, LoC::Common, LoC::Tank, LoC::SlotFace, 21, 110),
		Item_Base(1253, LoC::Common, LoC::Tank, LoC::SlotHead, 21, 110),
		Item_Base(1254, LoC::Common, LoC::Tank, LoC::SlotFingers, 21, 110),
		Item_Base(1255, LoC::Common, LoC::Tank, LoC::SlotWrist, 21, 110),
		Item_Base(1256, LoC::Common, LoC::Tank, LoC::SlotArms, 21, 110),
		Item_Base(1257, LoC::Common, LoC::Tank, LoC::SlotHand, 21, 110),
		Item_Base(1258, LoC::Common, LoC::Tank, LoC::SlotShoulders, 21, 110),
		Item_Base(1259, LoC::Common, LoC::Tank, LoC::SlotChest, 21, 110),
		Item_Base(1260, LoC::Common, LoC::Tank, LoC::SlotBack, 21, 110),
		Item_Base(1261, LoC::Common, LoC::Tank, LoC::SlotWaist, 21, 110),
		Item_Base(1262, LoC::Common, LoC::Tank, LoC::SlotLegs, 21, 110),
		Item_Base(1263, LoC::Common, LoC::Tank, LoC::SlotFeet, 21, 110),
		Item_Base(1264, LoC::Common, LoC::Tank, LoC::SlotCharm, 21, 110),
		Item_Base(1265, LoC::Common, LoC::Tank, LoC::SlotPowerSource, 21, 110),
		Item_Base(1266, LoC::Common, LoC::Tank, LoC::SlotOneHB, 21, 110),
		Item_Base(1267, LoC::Common, LoC::Tank, LoC::SlotTwoHB, 21, 110),
		Item_Base(1268, LoC::Common, LoC::Tank, LoC::SlotOneHS, 21, 110),
		Item_Base(1269, LoC::Common, LoC::Tank, LoC::SlotTwoHS, 21, 110),
		Item_Base(1270, LoC::Common, LoC::Tank, LoC::SlotArchery, 21, 110),
		Item_Base(1271, LoC::Common, LoC::Tank, LoC::SlotThrowing, 21, 110),
		Item_Base(1272, LoC::Common, LoC::Tank, LoC::SlotOneHP, 21, 110),
		Item_Base(1273, LoC::Common, LoC::Tank, LoC::SlotTwoHP, 21, 110),
		Item_Base(1274, LoC::Common, LoC::Tank, LoC::SlotShield, 21, 110),
		Item_Base(1275, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 10),
		Item_Base(1276, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 10),
		Item_Base(1277, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 10),
		Item_Base(1278, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 10),
		Item_Base(1279, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 10),
		Item_Base(1280, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 10),
		Item_Base(1281, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 10),
		Item_Base(1282, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 10),
		Item_Base(1283, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 10),
		Item_Base(1284, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 10),
		Item_Base(1285, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 10),
		Item_Base(1286, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 10),
		Item_Base(1287, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 10),
		Item_Base(1288, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 10),
		Item_Base(1289, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 10),
		Item_Base(1290, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 10),
		Item_Base(1291, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 10),
		Item_Base(1292, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 10),
		Item_Base(1293, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 10),
		Item_Base(1294, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 10),
		Item_Base(1295, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 10),
		Item_Base(1296, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 10),
		Item_Base(1297, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 10),
		Item_Base(1298, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 10),
		Item_Base(1299, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 10),
		Item_Base(1300, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 20),
		Item_Base(1301, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 20),
		Item_Base(1302, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 20),
		Item_Base(1303, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 20),
		Item_Base(1304, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 20),
		Item_Base(1305, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 20),
		Item_Base(1306, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 20),
		Item_Base(1307, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 20),
		Item_Base(1308, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 20),
		Item_Base(1309, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 20),
		Item_Base(1310, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 20),
		Item_Base(1311, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 20),
		Item_Base(1312, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 20),
		Item_Base(1313, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 20),
		Item_Base(1314, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 20),
		Item_Base(1315, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 20),
		Item_Base(1316, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 20),
		Item_Base(1317, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 20),
		Item_Base(1318, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 20),
		Item_Base(1319, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 20),
		Item_Base(1320, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 20),
		Item_Base(1321, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 20),
		Item_Base(1322, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 20),
		Item_Base(1323, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 20),
		Item_Base(1324, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 20),
		Item_Base(1325, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 30),
		Item_Base(1326, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 30),
		Item_Base(1327, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 30),
		Item_Base(1328, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 30),
		Item_Base(1329, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 30),
		Item_Base(1330, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 30),
		Item_Base(1331, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 30),
		Item_Base(1332, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 30),
		Item_Base(1333, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 30),
		Item_Base(1334, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 30),
		Item_Base(1335, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 30),
		Item_Base(1336, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 30),
		Item_Base(1337, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 30),
		Item_Base(1338, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 30),
		Item_Base(1339, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 30),
		Item_Base(1340, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 30),
		Item_Base(1341, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 30),
		Item_Base(1342, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 30),
		Item_Base(1343, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 30),
		Item_Base(1344, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 30),
		Item_Base(1345, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 30),
		Item_Base(1346, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 30),
		Item_Base(1347, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 30),
		Item_Base(1348, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 30),
		Item_Base(1349, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 30),
		Item_Base(1350, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 40),
		Item_Base(1351, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 40),
		Item_Base(1352, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 40),
		Item_Base(1353, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 40),
		Item_Base(1354, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 40),
		Item_Base(1355, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 40),
		Item_Base(1356, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 40),
		Item_Base(1357, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 40),
		Item_Base(1358, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 40),
		Item_Base(1359, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 40),
		Item_Base(1360, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 40),
		Item_Base(1361, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 40),
		Item_Base(1362, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 40),
		Item_Base(1363, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 40),
		Item_Base(1364, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 40),
		Item_Base(1365, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 40),
		Item_Base(1366, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 40),
		Item_Base(1367, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 40),
		Item_Base(1368, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 40),
		Item_Base(1369, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 40),
		Item_Base(1370, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 40),
		Item_Base(1371, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 40),
		Item_Base(1372, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 40),
		Item_Base(1373, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 40),
		Item_Base(1374, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 40),
		Item_Base(1375, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 50),
		Item_Base(1376, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 50),
		Item_Base(1377, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 50),
		Item_Base(1378, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 50),
		Item_Base(1379, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 50),
		Item_Base(1380, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 50),
		Item_Base(1381, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 50),
		Item_Base(1382, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 50),
		Item_Base(1383, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 50),
		Item_Base(1384, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 50),
		Item_Base(1385, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 50),
		Item_Base(1386, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 50),
		Item_Base(1387, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 50),
		Item_Base(1388, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 50),
		Item_Base(1389, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 50),
		Item_Base(1390, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 50),
		Item_Base(1391, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 50),
		Item_Base(1392, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 50),
		Item_Base(1393, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 50),
		Item_Base(1394, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 50),
		Item_Base(1395, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 50),
		Item_Base(1396, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 50),
		Item_Base(1397, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 50),
		Item_Base(1398, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 50),
		Item_Base(1399, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 50),
		Item_Base(1400, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 60),
		Item_Base(1401, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 60),
		Item_Base(1402, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 60),
		Item_Base(1403, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 60),
		Item_Base(1404, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 60),
		Item_Base(1405, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 60),
		Item_Base(1406, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 60),
		Item_Base(1407, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 60),
		Item_Base(1408, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 60),
		Item_Base(1409, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 60),
		Item_Base(1410, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 60),
		Item_Base(1411, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 60),
		Item_Base(1412, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 60),
		Item_Base(1413, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 60),
		Item_Base(1414, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 60),
		Item_Base(1415, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 60),
		Item_Base(1416, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 60),
		Item_Base(1417, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 60),
		Item_Base(1418, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 60),
		Item_Base(1419, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 60),
		Item_Base(1420, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 60),
		Item_Base(1421, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 60),
		Item_Base(1422, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 60),
		Item_Base(1423, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 60),
		Item_Base(1424, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 60),
		Item_Base(1425, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 70),
		Item_Base(1426, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 70),
		Item_Base(1427, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 70),
		Item_Base(1428, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 70),
		Item_Base(1429, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 70),
		Item_Base(1430, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 70),
		Item_Base(1431, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 70),
		Item_Base(1432, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 70),
		Item_Base(1433, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 70),
		Item_Base(1434, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 70),
		Item_Base(1435, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 70),
		Item_Base(1436, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 70),
		Item_Base(1437, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 70),
		Item_Base(1438, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 70),
		Item_Base(1439, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 70),
		Item_Base(1440, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 70),
		Item_Base(1441, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 70),
		Item_Base(1442, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 70),
		Item_Base(1443, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 70),
		Item_Base(1444, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 70),
		Item_Base(1445, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 70),
		Item_Base(1446, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 70),
		Item_Base(1447, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 70),
		Item_Base(1448, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 70),
		Item_Base(1449, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 70),
		Item_Base(1450, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 80),
		Item_Base(1451, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 80),
		Item_Base(1452, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 80),
		Item_Base(1453, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 80),
		Item_Base(1454, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 80),
		Item_Base(1455, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 80),
		Item_Base(1456, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 80),
		Item_Base(1457, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 80),
		Item_Base(1458, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 80),
		Item_Base(1459, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 80),
		Item_Base(1460, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 80),
		Item_Base(1461, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 80),
		Item_Base(1462, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 80),
		Item_Base(1463, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 80),
		Item_Base(1464, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 80),
		Item_Base(1465, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 80),
		Item_Base(1466, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 80),
		Item_Base(1467, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 80),
		Item_Base(1468, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 80),
		Item_Base(1469, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 80),
		Item_Base(1470, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 80),
		Item_Base(1471, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 80),
		Item_Base(1472, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 80),
		Item_Base(1473, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 80),
		Item_Base(1474, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 80),
		Item_Base(1475, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 90),
		Item_Base(1476, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 90),
		Item_Base(1477, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 90),
		Item_Base(1478, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 90),
		Item_Base(1479, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 90),
		Item_Base(1480, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 90),
		Item_Base(1481, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 90),
		Item_Base(1482, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 90),
		Item_Base(1483, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 90),
		Item_Base(1484, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 90),
		Item_Base(1485, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 90),
		Item_Base(1486, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 90),
		Item_Base(1487, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 90),
		Item_Base(1488, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 90),
		Item_Base(1489, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 90),
		Item_Base(1490, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 90),
		Item_Base(1491, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 90),
		Item_Base(1492, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 90),
		Item_Base(1493, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 90),
		Item_Base(1494, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 90),
		Item_Base(1495, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 90),
		Item_Base(1496, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 90),
		Item_Base(1497, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 90),
		Item_Base(1498, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 90),
		Item_Base(1499, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 90),
		Item_Base(1500, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 100),
		Item_Base(1501, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 100),
		Item_Base(1502, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 100),
		Item_Base(1503, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 100),
		Item_Base(1504, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 100),
		Item_Base(1505, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 100),
		Item_Base(1506, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 100),
		Item_Base(1507, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 100),
		Item_Base(1508, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 100),
		Item_Base(1509, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 100),
		Item_Base(1510, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 100),
		Item_Base(1511, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 100),
		Item_Base(1512, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 100),
		Item_Base(1513, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 100),
		Item_Base(1514, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 100),
		Item_Base(1515, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 100),
		Item_Base(1516, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 100),
		Item_Base(1517, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 100),
		Item_Base(1518, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 100),
		Item_Base(1519, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 100),
		Item_Base(1520, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 100),
		Item_Base(1521, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 100),
		Item_Base(1522, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 100),
		Item_Base(1523, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 100),
		Item_Base(1524, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 100),
		Item_Base(1525, LoC::Uncommon, LoC::Tank, LoC::SlotEar, 21, 110),
		Item_Base(1526, LoC::Uncommon, LoC::Tank, LoC::SlotNeck, 21, 110),
		Item_Base(1527, LoC::Uncommon, LoC::Tank, LoC::SlotFace, 21, 110),
		Item_Base(1528, LoC::Uncommon, LoC::Tank, LoC::SlotHead, 21, 110),
		Item_Base(1529, LoC::Uncommon, LoC::Tank, LoC::SlotFingers, 21, 110),
		Item_Base(1530, LoC::Uncommon, LoC::Tank, LoC::SlotWrist, 21, 110),
		Item_Base(1531, LoC::Uncommon, LoC::Tank, LoC::SlotArms, 21, 110),
		Item_Base(1532, LoC::Uncommon, LoC::Tank, LoC::SlotHand, 21, 110),
		Item_Base(1533, LoC::Uncommon, LoC::Tank, LoC::SlotShoulders, 21, 110),
		Item_Base(1534, LoC::Uncommon, LoC::Tank, LoC::SlotChest, 21, 110),
		Item_Base(1535, LoC::Uncommon, LoC::Tank, LoC::SlotBack, 21, 110),
		Item_Base(1536, LoC::Uncommon, LoC::Tank, LoC::SlotWaist, 21, 110),
		Item_Base(1537, LoC::Uncommon, LoC::Tank, LoC::SlotLegs, 21, 110),
		Item_Base(1538, LoC::Uncommon, LoC::Tank, LoC::SlotFeet, 21, 110),
		Item_Base(1539, LoC::Uncommon, LoC::Tank, LoC::SlotCharm, 21, 110),
		Item_Base(1540, LoC::Uncommon, LoC::Tank, LoC::SlotPowerSource, 21, 110),
		Item_Base(1541, LoC::Uncommon, LoC::Tank, LoC::SlotOneHB, 21, 110),
		Item_Base(1542, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHB, 21, 110),
		Item_Base(1543, LoC::Uncommon, LoC::Tank, LoC::SlotOneHS, 21, 110),
		Item_Base(1544, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHS, 21, 110),
		Item_Base(1545, LoC::Uncommon, LoC::Tank, LoC::SlotArchery, 21, 110),
		Item_Base(1546, LoC::Uncommon, LoC::Tank, LoC::SlotThrowing, 21, 110),
		Item_Base(1547, LoC::Uncommon, LoC::Tank, LoC::SlotOneHP, 21, 110),
		Item_Base(1548, LoC::Uncommon, LoC::Tank, LoC::SlotTwoHP, 21, 110),
		Item_Base(1549, LoC::Uncommon, LoC::Tank, LoC::SlotShield, 21, 110),
		Item_Base(1550, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 10),
		Item_Base(1551, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 10),
		Item_Base(1552, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 10),
		Item_Base(1553, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 10),
		Item_Base(1554, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 10),
		Item_Base(1555, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 10),
		Item_Base(1556, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 10),
		Item_Base(1557, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 10),
		Item_Base(1558, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 10),
		Item_Base(1559, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 10),
		Item_Base(1560, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 10),
		Item_Base(1561, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 10),
		Item_Base(1562, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 10),
		Item_Base(1563, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 10),
		Item_Base(1564, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 10),
		Item_Base(1565, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 10),
		Item_Base(1566, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 10),
		Item_Base(1567, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 10),
		Item_Base(1568, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 10),
		Item_Base(1569, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 10),
		Item_Base(1570, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 10),
		Item_Base(1571, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 10),
		Item_Base(1572, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 10),
		Item_Base(1573, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 10),
		Item_Base(1574, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 10),
		Item_Base(1575, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 20),
		Item_Base(1576, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 20),
		Item_Base(1577, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 20),
		Item_Base(1578, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 20),
		Item_Base(1579, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 20),
		Item_Base(1580, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 20),
		Item_Base(1581, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 20),
		Item_Base(1582, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 20),
		Item_Base(1583, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 20),
		Item_Base(1584, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 20),
		Item_Base(1585, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 20),
		Item_Base(1586, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 20),
		Item_Base(1587, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 20),
		Item_Base(1588, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 20),
		Item_Base(1589, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 20),
		Item_Base(1590, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 20),
		Item_Base(1591, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 20),
		Item_Base(1592, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 20),
		Item_Base(1593, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 20),
		Item_Base(1594, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 20),
		Item_Base(1595, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 20),
		Item_Base(1596, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 20),
		Item_Base(1597, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 20),
		Item_Base(1598, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 20),
		Item_Base(1599, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 20),
		Item_Base(1600, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 30),
		Item_Base(1601, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 30),
		Item_Base(1602, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 30),
		Item_Base(1603, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 30),
		Item_Base(1604, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 30),
		Item_Base(1605, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 30),
		Item_Base(1606, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 30),
		Item_Base(1607, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 30),
		Item_Base(1608, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 30),
		Item_Base(1609, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 30),
		Item_Base(1610, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 30),
		Item_Base(1611, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 30),
		Item_Base(1612, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 30),
		Item_Base(1613, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 30),
		Item_Base(1614, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 30),
		Item_Base(1615, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 30),
		Item_Base(1616, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 30),
		Item_Base(1617, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 30),
		Item_Base(1618, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 30),
		Item_Base(1619, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 30),
		Item_Base(1620, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 30),
		Item_Base(1621, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 30),
		Item_Base(1622, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 30),
		Item_Base(1623, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 30),
		Item_Base(1624, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 30),
		Item_Base(1625, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 40),
		Item_Base(1626, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 40),
		Item_Base(1627, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 40),
		Item_Base(1628, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 40),
		Item_Base(1629, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 40),
		Item_Base(1630, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 40),
		Item_Base(1631, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 40),
		Item_Base(1632, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 40),
		Item_Base(1633, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 40),
		Item_Base(1634, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 40),
		Item_Base(1635, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 40),
		Item_Base(1636, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 40),
		Item_Base(1637, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 40),
		Item_Base(1638, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 40),
		Item_Base(1639, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 40),
		Item_Base(1640, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 40),
		Item_Base(1641, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 40),
		Item_Base(1642, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 40),
		Item_Base(1643, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 40),
		Item_Base(1644, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 40),
		Item_Base(1645, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 40),
		Item_Base(1646, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 40),
		Item_Base(1647, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 40),
		Item_Base(1648, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 40),
		Item_Base(1649, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 40),
		Item_Base(1650, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 50),
		Item_Base(1651, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 50),
		Item_Base(1652, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 50),
		Item_Base(1653, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 50),
		Item_Base(1654, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 50),
		Item_Base(1655, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 50),
		Item_Base(1656, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 50),
		Item_Base(1657, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 50),
		Item_Base(1658, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 50),
		Item_Base(1659, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 50),
		Item_Base(1660, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 50),
		Item_Base(1661, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 50),
		Item_Base(1662, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 50),
		Item_Base(1663, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 50),
		Item_Base(1664, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 50),
		Item_Base(1665, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 50),
		Item_Base(1666, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 50),
		Item_Base(1667, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 50),
		Item_Base(1668, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 50),
		Item_Base(1669, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 50),
		Item_Base(1670, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 50),
		Item_Base(1671, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 50),
		Item_Base(1672, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 50),
		Item_Base(1673, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 50),
		Item_Base(1674, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 50),
		Item_Base(1675, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 60),
		Item_Base(1676, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 60),
		Item_Base(1677, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 60),
		Item_Base(1678, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 60),
		Item_Base(1679, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 60),
		Item_Base(1680, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 60),
		Item_Base(1681, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 60),
		Item_Base(1682, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 60),
		Item_Base(1683, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 60),
		Item_Base(1684, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 60),
		Item_Base(1685, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 60),
		Item_Base(1686, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 60),
		Item_Base(1687, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 60),
		Item_Base(1688, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 60),
		Item_Base(1689, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 60),
		Item_Base(1690, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 60),
		Item_Base(1691, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 60),
		Item_Base(1692, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 60),
		Item_Base(1693, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 60),
		Item_Base(1694, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 60),
		Item_Base(1695, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 60),
		Item_Base(1696, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 60),
		Item_Base(1697, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 60),
		Item_Base(1698, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 60),
		Item_Base(1699, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 60),
		Item_Base(1700, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 70),
		Item_Base(1701, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 70),
		Item_Base(1702, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 70),
		Item_Base(1703, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 70),
		Item_Base(1704, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 70),
		Item_Base(1705, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 70),
		Item_Base(1706, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 70),
		Item_Base(1707, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 70),
		Item_Base(1708, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 70),
		Item_Base(1709, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 70),
		Item_Base(1710, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 70),
		Item_Base(1711, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 70),
		Item_Base(1712, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 70),
		Item_Base(1713, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 70),
		Item_Base(1714, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 70),
		Item_Base(1715, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 70),
		Item_Base(1716, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 70),
		Item_Base(1717, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 70),
		Item_Base(1718, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 70),
		Item_Base(1719, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 70),
		Item_Base(1720, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 70),
		Item_Base(1721, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 70),
		Item_Base(1722, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 70),
		Item_Base(1723, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 70),
		Item_Base(1724, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 70),
		Item_Base(1725, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 80),
		Item_Base(1726, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 80),
		Item_Base(1727, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 80),
		Item_Base(1728, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 80),
		Item_Base(1729, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 80),
		Item_Base(1730, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 80),
		Item_Base(1731, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 80),
		Item_Base(1732, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 80),
		Item_Base(1733, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 80),
		Item_Base(1734, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 80),
		Item_Base(1735, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 80),
		Item_Base(1736, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 80),
		Item_Base(1737, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 80),
		Item_Base(1738, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 80),
		Item_Base(1739, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 80),
		Item_Base(1740, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 80),
		Item_Base(1741, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 80),
		Item_Base(1742, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 80),
		Item_Base(1743, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 80),
		Item_Base(1744, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 80),
		Item_Base(1745, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 80),
		Item_Base(1746, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 80),
		Item_Base(1747, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 80),
		Item_Base(1748, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 80),
		Item_Base(1749, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 80),
		Item_Base(1750, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 90),
		Item_Base(1751, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 90),
		Item_Base(1752, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 90),
		Item_Base(1753, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 90),
		Item_Base(1754, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 90),
		Item_Base(1755, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 90),
		Item_Base(1756, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 90),
		Item_Base(1757, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 90),
		Item_Base(1758, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 90),
		Item_Base(1759, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 90),
		Item_Base(1760, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 90),
		Item_Base(1761, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 90),
		Item_Base(1762, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 90),
		Item_Base(1763, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 90),
		Item_Base(1764, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 90),
		Item_Base(1765, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 90),
		Item_Base(1766, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 90),
		Item_Base(1767, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 90),
		Item_Base(1768, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 90),
		Item_Base(1769, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 90),
		Item_Base(1770, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 90),
		Item_Base(1771, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 90),
		Item_Base(1772, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 90),
		Item_Base(1773, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 90),
		Item_Base(1774, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 90),
		Item_Base(1775, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 100),
		Item_Base(1776, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 100),
		Item_Base(1777, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 100),
		Item_Base(1778, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 100),
		Item_Base(1779, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 100),
		Item_Base(1780, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 100),
		Item_Base(1781, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 100),
		Item_Base(1782, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 100),
		Item_Base(1783, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 100),
		Item_Base(1784, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 100),
		Item_Base(1785, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 100),
		Item_Base(1786, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 100),
		Item_Base(1787, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 100),
		Item_Base(1788, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 100),
		Item_Base(1789, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 100),
		Item_Base(1790, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 100),
		Item_Base(1791, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 100),
		Item_Base(1792, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 100),
		Item_Base(1793, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 100),
		Item_Base(1794, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 100),
		Item_Base(1795, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 100),
		Item_Base(1796, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 100),
		Item_Base(1797, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 100),
		Item_Base(1798, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 100),
		Item_Base(1799, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 100),
		Item_Base(1800, LoC::Rare, LoC::Tank, LoC::SlotEar, 21, 110),
		Item_Base(1801, LoC::Rare, LoC::Tank, LoC::SlotNeck, 21, 110),
		Item_Base(1802, LoC::Rare, LoC::Tank, LoC::SlotFace, 21, 110),
		Item_Base(1803, LoC::Rare, LoC::Tank, LoC::SlotHead, 21, 110),
		Item_Base(1804, LoC::Rare, LoC::Tank, LoC::SlotFingers, 21, 110),
		Item_Base(1805, LoC::Rare, LoC::Tank, LoC::SlotWrist, 21, 110),
		Item_Base(1806, LoC::Rare, LoC::Tank, LoC::SlotArms, 21, 110),
		Item_Base(1807, LoC::Rare, LoC::Tank, LoC::SlotHand, 21, 110),
		Item_Base(1808, LoC::Rare, LoC::Tank, LoC::SlotShoulders, 21, 110),
		Item_Base(1809, LoC::Rare, LoC::Tank, LoC::SlotChest, 21, 110),
		Item_Base(1810, LoC::Rare, LoC::Tank, LoC::SlotBack, 21, 110),
		Item_Base(1811, LoC::Rare, LoC::Tank, LoC::SlotWaist, 21, 110),
		Item_Base(1812, LoC::Rare, LoC::Tank, LoC::SlotLegs, 21, 110),
		Item_Base(1813, LoC::Rare, LoC::Tank, LoC::SlotFeet, 21, 110),
		Item_Base(1814, LoC::Rare, LoC::Tank, LoC::SlotCharm, 21, 110),
		Item_Base(1815, LoC::Rare, LoC::Tank, LoC::SlotPowerSource, 21, 110),
		Item_Base(1816, LoC::Rare, LoC::Tank, LoC::SlotOneHB, 21, 110),
		Item_Base(1817, LoC::Rare, LoC::Tank, LoC::SlotTwoHB, 21, 110),
		Item_Base(1818, LoC::Rare, LoC::Tank, LoC::SlotOneHS, 21, 110),
		Item_Base(1819, LoC::Rare, LoC::Tank, LoC::SlotTwoHS, 21, 110),
		Item_Base(1820, LoC::Rare, LoC::Tank, LoC::SlotArchery, 21, 110),
		Item_Base(1821, LoC::Rare, LoC::Tank, LoC::SlotThrowing, 21, 110),
		Item_Base(1822, LoC::Rare, LoC::Tank, LoC::SlotOneHP, 21, 110),
		Item_Base(1823, LoC::Rare, LoC::Tank, LoC::SlotTwoHP, 21, 110),
		Item_Base(1824, LoC::Rare, LoC::Tank, LoC::SlotShield, 21, 110),
		Item_Base(1825, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 10),
		Item_Base(1826, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 10),
		Item_Base(1827, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 10),
		Item_Base(1828, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 10),
		Item_Base(1829, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 10),
		Item_Base(1830, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 10),
		Item_Base(1831, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 10),
		Item_Base(1832, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 10),
		Item_Base(1833, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 10),
		Item_Base(1834, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 10),
		Item_Base(1835, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 10),
		Item_Base(1836, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 10),
		Item_Base(1837, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 10),
		Item_Base(1838, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 10),
		Item_Base(1839, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 10),
		Item_Base(1840, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 10),
		Item_Base(1841, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 10),
		Item_Base(1842, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 10),
		Item_Base(1843, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 10),
		Item_Base(1844, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 10),
		Item_Base(1845, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 10),
		Item_Base(1846, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 10),
		Item_Base(1847, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 10),
		Item_Base(1848, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 10),
		Item_Base(1849, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 10),
		Item_Base(1850, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 20),
		Item_Base(1851, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 20),
		Item_Base(1852, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 20),
		Item_Base(1853, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 20),
		Item_Base(1854, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 20),
		Item_Base(1855, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 20),
		Item_Base(1856, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 20),
		Item_Base(1857, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 20),
		Item_Base(1858, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 20),
		Item_Base(1859, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 20),
		Item_Base(1860, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 20),
		Item_Base(1861, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 20),
		Item_Base(1862, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 20),
		Item_Base(1863, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 20),
		Item_Base(1864, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 20),
		Item_Base(1865, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 20),
		Item_Base(1866, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 20),
		Item_Base(1867, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 20),
		Item_Base(1868, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 20),
		Item_Base(1869, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 20),
		Item_Base(1870, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 20),
		Item_Base(1871, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 20),
		Item_Base(1872, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 20),
		Item_Base(1873, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 20),
		Item_Base(1874, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 20),
		Item_Base(1875, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 30),
		Item_Base(1876, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 30),
		Item_Base(1877, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 30),
		Item_Base(1878, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 30),
		Item_Base(1879, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 30),
		Item_Base(1880, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 30),
		Item_Base(1881, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 30),
		Item_Base(1882, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 30),
		Item_Base(1883, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 30),
		Item_Base(1884, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 30),
		Item_Base(1885, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 30),
		Item_Base(1886, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 30),
		Item_Base(1887, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 30),
		Item_Base(1888, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 30),
		Item_Base(1889, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 30),
		Item_Base(1890, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 30),
		Item_Base(1891, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 30),
		Item_Base(1892, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 30),
		Item_Base(1893, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 30),
		Item_Base(1894, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 30),
		Item_Base(1895, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 30),
		Item_Base(1896, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 30),
		Item_Base(1897, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 30),
		Item_Base(1898, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 30),
		Item_Base(1899, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 30),
		Item_Base(1900, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 40),
		Item_Base(1901, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 40),
		Item_Base(1902, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 40),
		Item_Base(1903, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 40),
		Item_Base(1904, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 40),
		Item_Base(1905, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 40),
		Item_Base(1906, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 40),
		Item_Base(1907, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 40),
		Item_Base(1908, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 40),
		Item_Base(1909, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 40),
		Item_Base(1910, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 40),
		Item_Base(1911, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 40),
		Item_Base(1912, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 40),
		Item_Base(1913, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 40),
		Item_Base(1914, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 40),
		Item_Base(1915, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 40),
		Item_Base(1916, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 40),
		Item_Base(1917, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 40),
		Item_Base(1918, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 40),
		Item_Base(1919, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 40),
		Item_Base(1920, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 40),
		Item_Base(1921, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 40),
		Item_Base(1922, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 40),
		Item_Base(1923, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 40),
		Item_Base(1924, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 40),
		Item_Base(1925, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 50),
		Item_Base(1926, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 50),
		Item_Base(1927, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 50),
		Item_Base(1928, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 50),
		Item_Base(1929, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 50),
		Item_Base(1930, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 50),
		Item_Base(1931, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 50),
		Item_Base(1932, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 50),
		Item_Base(1933, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 50),
		Item_Base(1934, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 50),
		Item_Base(1935, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 50),
		Item_Base(1936, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 50),
		Item_Base(1937, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 50),
		Item_Base(1938, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 50),
		Item_Base(1939, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 50),
		Item_Base(1940, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 50),
		Item_Base(1941, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 50),
		Item_Base(1942, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 50),
		Item_Base(1943, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 50),
		Item_Base(1944, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 50),
		Item_Base(1945, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 50),
		Item_Base(1946, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 50),
		Item_Base(1947, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 50),
		Item_Base(1948, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 50),
		Item_Base(1949, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 50),
		Item_Base(1950, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 60),
		Item_Base(1951, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 60),
		Item_Base(1952, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 60),
		Item_Base(1953, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 60),
		Item_Base(1954, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 60),
		Item_Base(1955, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 60),
		Item_Base(1956, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 60),
		Item_Base(1957, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 60),
		Item_Base(1958, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 60),
		Item_Base(1959, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 60),
		Item_Base(1960, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 60),
		Item_Base(1961, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 60),
		Item_Base(1962, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 60),
		Item_Base(1963, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 60),
		Item_Base(1964, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 60),
		Item_Base(1965, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 60),
		Item_Base(1966, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 60),
		Item_Base(1967, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 60),
		Item_Base(1968, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 60),
		Item_Base(1969, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 60),
		Item_Base(1970, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 60),
		Item_Base(1971, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 60),
		Item_Base(1972, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 60),
		Item_Base(1973, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 60),
		Item_Base(1974, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 60),
		Item_Base(1975, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 70),
		Item_Base(1976, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 70),
		Item_Base(1977, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 70),
		Item_Base(1978, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 70),
		Item_Base(1979, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 70),
		Item_Base(1980, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 70),
		Item_Base(1981, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 70),
		Item_Base(1982, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 70),
		Item_Base(1983, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 70),
		Item_Base(1984, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 70),
		Item_Base(1985, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 70),
		Item_Base(1986, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 70),
		Item_Base(1987, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 70),
		Item_Base(1988, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 70),
		Item_Base(1989, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 70),
		Item_Base(1990, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 70),
		Item_Base(1991, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 70),
		Item_Base(1992, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 70),
		Item_Base(1993, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 70),
		Item_Base(1994, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 70),
		Item_Base(1995, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 70),
		Item_Base(1996, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 70),
		Item_Base(1997, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 70),
		Item_Base(1998, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 70),
		Item_Base(1999, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 70),
		Item_Base(2000, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 80),
		Item_Base(2001, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 80),
		Item_Base(2002, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 80),
		Item_Base(2003, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 80),
		Item_Base(2004, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 80),
		Item_Base(2005, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 80),
		Item_Base(2006, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 80),
		Item_Base(2007, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 80),
		Item_Base(2008, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 80),
		Item_Base(2009, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 80),
		Item_Base(2010, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 80),
		Item_Base(2011, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 80),
		Item_Base(2012, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 80),
		Item_Base(2013, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 80),
		Item_Base(2014, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 80),
		Item_Base(2015, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 80),
		Item_Base(2016, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 80),
		Item_Base(2017, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 80),
		Item_Base(2018, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 80),
		Item_Base(2019, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 80),
		Item_Base(2020, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 80),
		Item_Base(2021, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 80),
		Item_Base(2022, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 80),
		Item_Base(2023, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 80),
		Item_Base(2024, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 80),
		Item_Base(2025, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 90),
		Item_Base(2026, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 90),
		Item_Base(2027, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 90),
		Item_Base(2028, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 90),
		Item_Base(2029, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 90),
		Item_Base(2030, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 90),
		Item_Base(2031, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 90),
		Item_Base(2032, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 90),
		Item_Base(2033, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 90),
		Item_Base(2034, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 90),
		Item_Base(2035, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 90),
		Item_Base(2036, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 90),
		Item_Base(2037, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 90),
		Item_Base(2038, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 90),
		Item_Base(2039, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 90),
		Item_Base(2040, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 90),
		Item_Base(2041, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 90),
		Item_Base(2042, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 90),
		Item_Base(2043, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 90),
		Item_Base(2044, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 90),
		Item_Base(2045, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 90),
		Item_Base(2046, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 90),
		Item_Base(2047, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 90),
		Item_Base(2048, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 90),
		Item_Base(2049, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 90),
		Item_Base(2050, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 100),
		Item_Base(2051, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 100),
		Item_Base(2052, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 100),
		Item_Base(2053, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 100),
		Item_Base(2054, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 100),
		Item_Base(2055, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 100),
		Item_Base(2056, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 100),
		Item_Base(2057, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 100),
		Item_Base(2058, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 100),
		Item_Base(2059, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 100),
		Item_Base(2060, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 100),
		Item_Base(2061, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 100),
		Item_Base(2062, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 100),
		Item_Base(2063, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 100),
		Item_Base(2064, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 100),
		Item_Base(2065, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 100),
		Item_Base(2066, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 100),
		Item_Base(2067, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 100),
		Item_Base(2068, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 100),
		Item_Base(2069, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 100),
		Item_Base(2070, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 100),
		Item_Base(2071, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 100),
		Item_Base(2072, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 100),
		Item_Base(2073, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 100),
		Item_Base(2074, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 100),
		Item_Base(2075, LoC::Legendary, LoC::Tank, LoC::SlotEar, 21, 110),
		Item_Base(2076, LoC::Legendary, LoC::Tank, LoC::SlotNeck, 21, 110),
		Item_Base(2077, LoC::Legendary, LoC::Tank, LoC::SlotFace, 21, 110),
		Item_Base(2078, LoC::Legendary, LoC::Tank, LoC::SlotHead, 21, 110),
		Item_Base(2079, LoC::Legendary, LoC::Tank, LoC::SlotFingers, 21, 110),
		Item_Base(2080, LoC::Legendary, LoC::Tank, LoC::SlotWrist, 21, 110),
		Item_Base(2081, LoC::Legendary, LoC::Tank, LoC::SlotArms, 21, 110),
		Item_Base(2082, LoC::Legendary, LoC::Tank, LoC::SlotHand, 21, 110),
		Item_Base(2083, LoC::Legendary, LoC::Tank, LoC::SlotShoulders, 21, 110),
		Item_Base(2084, LoC::Legendary, LoC::Tank, LoC::SlotChest, 21, 110),
		Item_Base(2085, LoC::Legendary, LoC::Tank, LoC::SlotBack, 21, 110),
		Item_Base(2086, LoC::Legendary, LoC::Tank, LoC::SlotWaist, 21, 110),
		Item_Base(2087, LoC::Legendary, LoC::Tank, LoC::SlotLegs, 21, 110),
		Item_Base(2088, LoC::Legendary, LoC::Tank, LoC::SlotFeet, 21, 110),
		Item_Base(2089, LoC::Legendary, LoC::Tank, LoC::SlotCharm, 21, 110),
		Item_Base(2090, LoC::Legendary, LoC::Tank, LoC::SlotPowerSource, 21, 110),
		Item_Base(2091, LoC::Legendary, LoC::Tank, LoC::SlotOneHB, 21, 110),
		Item_Base(2092, LoC::Legendary, LoC::Tank, LoC::SlotTwoHB, 21, 110),
		Item_Base(2093, LoC::Legendary, LoC::Tank, LoC::SlotOneHS, 21, 110),
		Item_Base(2094, LoC::Legendary, LoC::Tank, LoC::SlotTwoHS, 21, 110),
		Item_Base(2095, LoC::Legendary, LoC::Tank, LoC::SlotArchery, 21, 110),
		Item_Base(2096, LoC::Legendary, LoC::Tank, LoC::SlotThrowing, 21, 110),
		Item_Base(2097, LoC::Legendary, LoC::Tank, LoC::SlotOneHP, 21, 110),
		Item_Base(2098, LoC::Legendary, LoC::Tank, LoC::SlotTwoHP, 21, 110),
		Item_Base(2099, LoC::Legendary, LoC::Tank, LoC::SlotShield, 21, 110),
		Item_Base(2100, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 10),
		Item_Base(2101, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 10),
		Item_Base(2102, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 10),
		Item_Base(2103, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 10),
		Item_Base(2104, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 10),
		Item_Base(2105, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 10),
		Item_Base(2106, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 10),
		Item_Base(2107, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 10),
		Item_Base(2108, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 10),
		Item_Base(2109, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 10),
		Item_Base(2110, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 10),
		Item_Base(2111, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 10),
		Item_Base(2112, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 10),
		Item_Base(2113, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 10),
		Item_Base(2114, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 10),
		Item_Base(2115, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 10),
		Item_Base(2116, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 10),
		Item_Base(2117, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 10),
		Item_Base(2118, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 10),
		Item_Base(2119, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 10),
		Item_Base(2120, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 10),
		Item_Base(2121, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 10),
		Item_Base(2122, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 10),
		Item_Base(2123, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 10),
		Item_Base(2124, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 10),
		Item_Base(2125, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 20),
		Item_Base(2126, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 20),
		Item_Base(2127, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 20),
		Item_Base(2128, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 20),
		Item_Base(2129, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 20),
		Item_Base(2130, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 20),
		Item_Base(2131, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 20),
		Item_Base(2132, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 20),
		Item_Base(2133, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 20),
		Item_Base(2134, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 20),
		Item_Base(2135, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 20),
		Item_Base(2136, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 20),
		Item_Base(2137, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 20),
		Item_Base(2138, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 20),
		Item_Base(2139, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 20),
		Item_Base(2140, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 20),
		Item_Base(2141, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 20),
		Item_Base(2142, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 20),
		Item_Base(2143, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 20),
		Item_Base(2144, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 20),
		Item_Base(2145, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 20),
		Item_Base(2146, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 20),
		Item_Base(2147, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 20),
		Item_Base(2148, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 20),
		Item_Base(2149, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 20),
		Item_Base(2150, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 30),
		Item_Base(2151, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 30),
		Item_Base(2152, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 30),
		Item_Base(2153, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 30),
		Item_Base(2154, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 30),
		Item_Base(2155, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 30),
		Item_Base(2156, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 30),
		Item_Base(2157, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 30),
		Item_Base(2158, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 30),
		Item_Base(2159, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 30),
		Item_Base(2160, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 30),
		Item_Base(2161, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 30),
		Item_Base(2162, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 30),
		Item_Base(2163, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 30),
		Item_Base(2164, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 30),
		Item_Base(2165, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 30),
		Item_Base(2166, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 30),
		Item_Base(2167, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 30),
		Item_Base(2168, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 30),
		Item_Base(2169, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 30),
		Item_Base(2170, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 30),
		Item_Base(2171, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 30),
		Item_Base(2172, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 30),
		Item_Base(2173, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 30),
		Item_Base(2174, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 30),
		Item_Base(2175, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 40),
		Item_Base(2176, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 40),
		Item_Base(2177, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 40),
		Item_Base(2178, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 40),
		Item_Base(2179, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 40),
		Item_Base(2180, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 40),
		Item_Base(2181, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 40),
		Item_Base(2182, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 40),
		Item_Base(2183, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 40),
		Item_Base(2184, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 40),
		Item_Base(2185, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 40),
		Item_Base(2186, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 40),
		Item_Base(2187, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 40),
		Item_Base(2188, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 40),
		Item_Base(2189, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 40),
		Item_Base(2190, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 40),
		Item_Base(2191, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 40),
		Item_Base(2192, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 40),
		Item_Base(2193, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 40),
		Item_Base(2194, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 40),
		Item_Base(2195, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 40),
		Item_Base(2196, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 40),
		Item_Base(2197, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 40),
		Item_Base(2198, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 40),
		Item_Base(2199, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 40),
		Item_Base(2200, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 50),
		Item_Base(2201, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 50),
		Item_Base(2202, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 50),
		Item_Base(2203, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 50),
		Item_Base(2204, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 50),
		Item_Base(2205, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 50),
		Item_Base(2206, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 50),
		Item_Base(2207, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 50),
		Item_Base(2208, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 50),
		Item_Base(2209, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 50),
		Item_Base(2210, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 50),
		Item_Base(2211, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 50),
		Item_Base(2212, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 50),
		Item_Base(2213, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 50),
		Item_Base(2214, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 50),
		Item_Base(2215, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 50),
		Item_Base(2216, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 50),
		Item_Base(2217, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 50),
		Item_Base(2218, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 50),
		Item_Base(2219, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 50),
		Item_Base(2220, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 50),
		Item_Base(2221, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 50),
		Item_Base(2222, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 50),
		Item_Base(2223, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 50),
		Item_Base(2224, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 50),
		Item_Base(2225, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 60),
		Item_Base(2226, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 60),
		Item_Base(2227, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 60),
		Item_Base(2228, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 60),
		Item_Base(2229, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 60),
		Item_Base(2230, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 60),
		Item_Base(2231, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 60),
		Item_Base(2232, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 60),
		Item_Base(2233, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 60),
		Item_Base(2234, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 60),
		Item_Base(2235, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 60),
		Item_Base(2236, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 60),
		Item_Base(2237, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 60),
		Item_Base(2238, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 60),
		Item_Base(2239, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 60),
		Item_Base(2240, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 60),
		Item_Base(2241, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 60),
		Item_Base(2242, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 60),
		Item_Base(2243, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 60),
		Item_Base(2244, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 60),
		Item_Base(2245, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 60),
		Item_Base(2246, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 60),
		Item_Base(2247, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 60),
		Item_Base(2248, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 60),
		Item_Base(2249, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 60),
		Item_Base(2250, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 70),
		Item_Base(2251, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 70),
		Item_Base(2252, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 70),
		Item_Base(2253, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 70),
		Item_Base(2254, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 70),
		Item_Base(2255, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 70),
		Item_Base(2256, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 70),
		Item_Base(2257, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 70),
		Item_Base(2258, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 70),
		Item_Base(2259, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 70),
		Item_Base(2260, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 70),
		Item_Base(2261, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 70),
		Item_Base(2262, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 70),
		Item_Base(2263, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 70),
		Item_Base(2264, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 70),
		Item_Base(2265, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 70),
		Item_Base(2266, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 70),
		Item_Base(2267, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 70),
		Item_Base(2268, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 70),
		Item_Base(2269, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 70),
		Item_Base(2270, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 70),
		Item_Base(2271, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 70),
		Item_Base(2272, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 70),
		Item_Base(2273, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 70),
		Item_Base(2274, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 70),
		Item_Base(2275, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 80),
		Item_Base(2276, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 80),
		Item_Base(2277, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 80),
		Item_Base(2278, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 80),
		Item_Base(2279, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 80),
		Item_Base(2280, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 80),
		Item_Base(2281, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 80),
		Item_Base(2282, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 80),
		Item_Base(2283, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 80),
		Item_Base(2284, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 80),
		Item_Base(2285, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 80),
		Item_Base(2286, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 80),
		Item_Base(2287, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 80),
		Item_Base(2288, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 80),
		Item_Base(2289, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 80),
		Item_Base(2290, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 80),
		Item_Base(2291, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 80),
		Item_Base(2292, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 80),
		Item_Base(2293, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 80),
		Item_Base(2294, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 80),
		Item_Base(2295, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 80),
		Item_Base(2296, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 80),
		Item_Base(2297, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 80),
		Item_Base(2298, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 80),
		Item_Base(2299, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 80),
		Item_Base(2300, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 90),
		Item_Base(2301, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 90),
		Item_Base(2302, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 90),
		Item_Base(2303, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 90),
		Item_Base(2304, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 90),
		Item_Base(2305, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 90),
		Item_Base(2306, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 90),
		Item_Base(2307, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 90),
		Item_Base(2308, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 90),
		Item_Base(2309, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 90),
		Item_Base(2310, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 90),
		Item_Base(2311, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 90),
		Item_Base(2312, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 90),
		Item_Base(2313, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 90),
		Item_Base(2314, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 90),
		Item_Base(2315, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 90),
		Item_Base(2316, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 90),
		Item_Base(2317, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 90),
		Item_Base(2318, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 90),
		Item_Base(2319, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 90),
		Item_Base(2320, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 90),
		Item_Base(2321, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 90),
		Item_Base(2322, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 90),
		Item_Base(2323, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 90),
		Item_Base(2324, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 90),
		Item_Base(2325, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 100),
		Item_Base(2326, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 100),
		Item_Base(2327, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 100),
		Item_Base(2328, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 100),
		Item_Base(2329, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 100),
		Item_Base(2330, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 100),
		Item_Base(2331, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 100),
		Item_Base(2332, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 100),
		Item_Base(2333, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 100),
		Item_Base(2334, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 100),
		Item_Base(2335, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 100),
		Item_Base(2336, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 100),
		Item_Base(2337, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 100),
		Item_Base(2338, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 100),
		Item_Base(2339, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 100),
		Item_Base(2340, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 100),
		Item_Base(2341, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 100),
		Item_Base(2342, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 100),
		Item_Base(2343, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 100),
		Item_Base(2344, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 100),
		Item_Base(2345, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 100),
		Item_Base(2346, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 100),
		Item_Base(2347, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 100),
		Item_Base(2348, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 100),
		Item_Base(2349, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 100),
		Item_Base(2350, LoC::Unique, LoC::Tank, LoC::SlotEar, 21, 110),
		Item_Base(2351, LoC::Unique, LoC::Tank, LoC::SlotNeck, 21, 110),
		Item_Base(2352, LoC::Unique, LoC::Tank, LoC::SlotFace, 21, 110),
		Item_Base(2353, LoC::Unique, LoC::Tank, LoC::SlotHead, 21, 110),
		Item_Base(2354, LoC::Unique, LoC::Tank, LoC::SlotFingers, 21, 110),
		Item_Base(2355, LoC::Unique, LoC::Tank, LoC::SlotWrist, 21, 110),
		Item_Base(2356, LoC::Unique, LoC::Tank, LoC::SlotArms, 21, 110),
		Item_Base(2357, LoC::Unique, LoC::Tank, LoC::SlotHand, 21, 110),
		Item_Base(2358, LoC::Unique, LoC::Tank, LoC::SlotShoulders, 21, 110),
		Item_Base(2359, LoC::Unique, LoC::Tank, LoC::SlotChest, 21, 110),
		Item_Base(2360, LoC::Unique, LoC::Tank, LoC::SlotBack, 21, 110),
		Item_Base(2361, LoC::Unique, LoC::Tank, LoC::SlotWaist, 21, 110),
		Item_Base(2362, LoC::Unique, LoC::Tank, LoC::SlotLegs, 21, 110),
		Item_Base(2363, LoC::Unique, LoC::Tank, LoC::SlotFeet, 21, 110),
		Item_Base(2364, LoC::Unique, LoC::Tank, LoC::SlotCharm, 21, 110),
		Item_Base(2365, LoC::Unique, LoC::Tank, LoC::SlotPowerSource, 21, 110),
		Item_Base(2366, LoC::Unique, LoC::Tank, LoC::SlotOneHB, 21, 110),
		Item_Base(2367, LoC::Unique, LoC::Tank, LoC::SlotTwoHB, 21, 110),
		Item_Base(2368, LoC::Unique, LoC::Tank, LoC::SlotOneHS, 21, 110),
		Item_Base(2369, LoC::Unique, LoC::Tank, LoC::SlotTwoHS, 21, 110),
		Item_Base(2370, LoC::Unique, LoC::Tank, LoC::SlotArchery, 21, 110),
		Item_Base(2371, LoC::Unique, LoC::Tank, LoC::SlotThrowing, 21, 110),
		Item_Base(2372, LoC::Unique, LoC::Tank, LoC::SlotOneHP, 21, 110),
		Item_Base(2373, LoC::Unique, LoC::Tank, LoC::SlotTwoHP, 21, 110),
		Item_Base(2374, LoC::Unique, LoC::Tank, LoC::SlotShield, 21, 110),
		Item_Base(2375, LoC::Common, LoC::Support, LoC::SlotEar, 546, 10),
		Item_Base(2376, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 10),
		Item_Base(2377, LoC::Common, LoC::Support, LoC::SlotFace, 546, 10),
		Item_Base(2378, LoC::Common, LoC::Support, LoC::SlotHead, 546, 10),
		Item_Base(2379, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 10),
		Item_Base(2380, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 10),
		Item_Base(2381, LoC::Common, LoC::Support, LoC::SlotArms, 546, 10),
		Item_Base(2382, LoC::Common, LoC::Support, LoC::SlotHand, 546, 10),
		Item_Base(2383, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 10),
		Item_Base(2384, LoC::Common, LoC::Support, LoC::SlotChest, 546, 10),
		Item_Base(2385, LoC::Common, LoC::Support, LoC::SlotBack, 546, 10),
		Item_Base(2386, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 10),
		Item_Base(2387, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 10),
		Item_Base(2388, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 10),
		Item_Base(2389, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 10),
		Item_Base(2390, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 10),
		Item_Base(2391, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 10),
		Item_Base(2392, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 10),
		Item_Base(2393, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 10),
		Item_Base(2394, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 10),
		Item_Base(2395, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 10),
		Item_Base(2396, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 10),
		Item_Base(2397, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 10),
		Item_Base(2398, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 10),
		Item_Base(2399, LoC::Common, LoC::Support, LoC::SlotShield, 546, 10),
		Item_Base(2400, LoC::Common, LoC::Support, LoC::SlotEar, 546, 20),
		Item_Base(2401, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 20),
		Item_Base(2402, LoC::Common, LoC::Support, LoC::SlotFace, 546, 20),
		Item_Base(2403, LoC::Common, LoC::Support, LoC::SlotHead, 546, 20),
		Item_Base(2404, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 20),
		Item_Base(2405, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 20),
		Item_Base(2406, LoC::Common, LoC::Support, LoC::SlotArms, 546, 20),
		Item_Base(2407, LoC::Common, LoC::Support, LoC::SlotHand, 546, 20),
		Item_Base(2408, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 20),
		Item_Base(2409, LoC::Common, LoC::Support, LoC::SlotChest, 546, 20),
		Item_Base(2410, LoC::Common, LoC::Support, LoC::SlotBack, 546, 20),
		Item_Base(2411, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 20),
		Item_Base(2412, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 20),
		Item_Base(2413, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 20),
		Item_Base(2414, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 20),
		Item_Base(2415, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 20),
		Item_Base(2416, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 20),
		Item_Base(2417, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 20),
		Item_Base(2418, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 20),
		Item_Base(2419, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 20),
		Item_Base(2420, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 20),
		Item_Base(2421, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 20),
		Item_Base(2422, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 20),
		Item_Base(2423, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 20),
		Item_Base(2424, LoC::Common, LoC::Support, LoC::SlotShield, 546, 20),
		Item_Base(2425, LoC::Common, LoC::Support, LoC::SlotEar, 546, 30),
		Item_Base(2426, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 30),
		Item_Base(2427, LoC::Common, LoC::Support, LoC::SlotFace, 546, 30),
		Item_Base(2428, LoC::Common, LoC::Support, LoC::SlotHead, 546, 30),
		Item_Base(2429, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 30),
		Item_Base(2430, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 30),
		Item_Base(2431, LoC::Common, LoC::Support, LoC::SlotArms, 546, 30),
		Item_Base(2432, LoC::Common, LoC::Support, LoC::SlotHand, 546, 30),
		Item_Base(2433, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 30),
		Item_Base(2434, LoC::Common, LoC::Support, LoC::SlotChest, 546, 30),
		Item_Base(2435, LoC::Common, LoC::Support, LoC::SlotBack, 546, 30),
		Item_Base(2436, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 30),
		Item_Base(2437, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 30),
		Item_Base(2438, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 30),
		Item_Base(2439, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 30),
		Item_Base(2440, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 30),
		Item_Base(2441, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 30),
		Item_Base(2442, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 30),
		Item_Base(2443, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 30),
		Item_Base(2444, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 30),
		Item_Base(2445, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 30),
		Item_Base(2446, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 30),
		Item_Base(2447, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 30),
		Item_Base(2448, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 30),
		Item_Base(2449, LoC::Common, LoC::Support, LoC::SlotShield, 546, 30),
		Item_Base(2450, LoC::Common, LoC::Support, LoC::SlotEar, 546, 40),
		Item_Base(2451, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 40),
		Item_Base(2452, LoC::Common, LoC::Support, LoC::SlotFace, 546, 40),
		Item_Base(2453, LoC::Common, LoC::Support, LoC::SlotHead, 546, 40),
		Item_Base(2454, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 40),
		Item_Base(2455, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 40),
		Item_Base(2456, LoC::Common, LoC::Support, LoC::SlotArms, 546, 40),
		Item_Base(2457, LoC::Common, LoC::Support, LoC::SlotHand, 546, 40),
		Item_Base(2458, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 40),
		Item_Base(2459, LoC::Common, LoC::Support, LoC::SlotChest, 546, 40),
		Item_Base(2460, LoC::Common, LoC::Support, LoC::SlotBack, 546, 40),
		Item_Base(2461, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 40),
		Item_Base(2462, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 40),
		Item_Base(2463, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 40),
		Item_Base(2464, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 40),
		Item_Base(2465, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 40),
		Item_Base(2466, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 40),
		Item_Base(2467, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 40),
		Item_Base(2468, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 40),
		Item_Base(2469, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 40),
		Item_Base(2470, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 40),
		Item_Base(2471, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 40),
		Item_Base(2472, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 40),
		Item_Base(2473, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 40),
		Item_Base(2474, LoC::Common, LoC::Support, LoC::SlotShield, 546, 40),
		Item_Base(2475, LoC::Common, LoC::Support, LoC::SlotEar, 546, 50),
		Item_Base(2476, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 50),
		Item_Base(2477, LoC::Common, LoC::Support, LoC::SlotFace, 546, 50),
		Item_Base(2478, LoC::Common, LoC::Support, LoC::SlotHead, 546, 50),
		Item_Base(2479, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 50),
		Item_Base(2480, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 50),
		Item_Base(2481, LoC::Common, LoC::Support, LoC::SlotArms, 546, 50),
		Item_Base(2482, LoC::Common, LoC::Support, LoC::SlotHand, 546, 50),
		Item_Base(2483, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 50),
		Item_Base(2484, LoC::Common, LoC::Support, LoC::SlotChest, 546, 50),
		Item_Base(2485, LoC::Common, LoC::Support, LoC::SlotBack, 546, 50),
		Item_Base(2486, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 50),
		Item_Base(2487, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 50),
		Item_Base(2488, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 50),
		Item_Base(2489, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 50),
		Item_Base(2490, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 50),
		Item_Base(2491, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 50),
		Item_Base(2492, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 50),
		Item_Base(2493, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 50),
		Item_Base(2494, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 50),
		Item_Base(2495, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 50),
		Item_Base(2496, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 50),
		Item_Base(2497, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 50),
		Item_Base(2498, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 50),
		Item_Base(2499, LoC::Common, LoC::Support, LoC::SlotShield, 546, 50),
		Item_Base(2500, LoC::Common, LoC::Support, LoC::SlotEar, 546, 60),
		Item_Base(2501, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 60),
		Item_Base(2502, LoC::Common, LoC::Support, LoC::SlotFace, 546, 60),
		Item_Base(2503, LoC::Common, LoC::Support, LoC::SlotHead, 546, 60),
		Item_Base(2504, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 60),
		Item_Base(2505, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 60),
		Item_Base(2506, LoC::Common, LoC::Support, LoC::SlotArms, 546, 60),
		Item_Base(2507, LoC::Common, LoC::Support, LoC::SlotHand, 546, 60),
		Item_Base(2508, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 60),
		Item_Base(2509, LoC::Common, LoC::Support, LoC::SlotChest, 546, 60),
		Item_Base(2510, LoC::Common, LoC::Support, LoC::SlotBack, 546, 60),
		Item_Base(2511, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 60),
		Item_Base(2512, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 60),
		Item_Base(2513, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 60),
		Item_Base(2514, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 60),
		Item_Base(2515, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 60),
		Item_Base(2516, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 60),
		Item_Base(2517, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 60),
		Item_Base(2518, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 60),
		Item_Base(2519, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 60),
		Item_Base(2520, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 60),
		Item_Base(2521, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 60),
		Item_Base(2522, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 60),
		Item_Base(2523, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 60),
		Item_Base(2524, LoC::Common, LoC::Support, LoC::SlotShield, 546, 60),
		Item_Base(2525, LoC::Common, LoC::Support, LoC::SlotEar, 546, 70),
		Item_Base(2526, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 70),
		Item_Base(2527, LoC::Common, LoC::Support, LoC::SlotFace, 546, 70),
		Item_Base(2528, LoC::Common, LoC::Support, LoC::SlotHead, 546, 70),
		Item_Base(2529, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 70),
		Item_Base(2530, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 70),
		Item_Base(2531, LoC::Common, LoC::Support, LoC::SlotArms, 546, 70),
		Item_Base(2532, LoC::Common, LoC::Support, LoC::SlotHand, 546, 70),
		Item_Base(2533, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 70),
		Item_Base(2534, LoC::Common, LoC::Support, LoC::SlotChest, 546, 70),
		Item_Base(2535, LoC::Common, LoC::Support, LoC::SlotBack, 546, 70),
		Item_Base(2536, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 70),
		Item_Base(2537, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 70),
		Item_Base(2538, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 70),
		Item_Base(2539, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 70),
		Item_Base(2540, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 70),
		Item_Base(2541, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 70),
		Item_Base(2542, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 70),
		Item_Base(2543, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 70),
		Item_Base(2544, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 70),
		Item_Base(2545, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 70),
		Item_Base(2546, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 70),
		Item_Base(2547, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 70),
		Item_Base(2548, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 70),
		Item_Base(2549, LoC::Common, LoC::Support, LoC::SlotShield, 546, 70),
		Item_Base(2550, LoC::Common, LoC::Support, LoC::SlotEar, 546, 80),
		Item_Base(2551, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 80),
		Item_Base(2552, LoC::Common, LoC::Support, LoC::SlotFace, 546, 80),
		Item_Base(2553, LoC::Common, LoC::Support, LoC::SlotHead, 546, 80),
		Item_Base(2554, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 80),
		Item_Base(2555, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 80),
		Item_Base(2556, LoC::Common, LoC::Support, LoC::SlotArms, 546, 80),
		Item_Base(2557, LoC::Common, LoC::Support, LoC::SlotHand, 546, 80),
		Item_Base(2558, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 80),
		Item_Base(2559, LoC::Common, LoC::Support, LoC::SlotChest, 546, 80),
		Item_Base(2560, LoC::Common, LoC::Support, LoC::SlotBack, 546, 80),
		Item_Base(2561, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 80),
		Item_Base(2562, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 80),
		Item_Base(2563, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 80),
		Item_Base(2564, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 80),
		Item_Base(2565, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 80),
		Item_Base(2566, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 80),
		Item_Base(2567, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 80),
		Item_Base(2568, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 80),
		Item_Base(2569, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 80),
		Item_Base(2570, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 80),
		Item_Base(2571, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 80),
		Item_Base(2572, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 80),
		Item_Base(2573, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 80),
		Item_Base(2574, LoC::Common, LoC::Support, LoC::SlotShield, 546, 80),
		Item_Base(2575, LoC::Common, LoC::Support, LoC::SlotEar, 546, 90),
		Item_Base(2576, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 90),
		Item_Base(2577, LoC::Common, LoC::Support, LoC::SlotFace, 546, 90),
		Item_Base(2578, LoC::Common, LoC::Support, LoC::SlotHead, 546, 90),
		Item_Base(2579, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 90),
		Item_Base(2580, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 90),
		Item_Base(2581, LoC::Common, LoC::Support, LoC::SlotArms, 546, 90),
		Item_Base(2582, LoC::Common, LoC::Support, LoC::SlotHand, 546, 90),
		Item_Base(2583, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 90),
		Item_Base(2584, LoC::Common, LoC::Support, LoC::SlotChest, 546, 90),
		Item_Base(2585, LoC::Common, LoC::Support, LoC::SlotBack, 546, 90),
		Item_Base(2586, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 90),
		Item_Base(2587, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 90),
		Item_Base(2588, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 90),
		Item_Base(2589, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 90),
		Item_Base(2590, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 90),
		Item_Base(2591, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 90),
		Item_Base(2592, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 90),
		Item_Base(2593, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 90),
		Item_Base(2594, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 90),
		Item_Base(2595, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 90),
		Item_Base(2596, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 90),
		Item_Base(2597, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 90),
		Item_Base(2598, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 90),
		Item_Base(2599, LoC::Common, LoC::Support, LoC::SlotShield, 546, 90),
		Item_Base(2600, LoC::Common, LoC::Support, LoC::SlotEar, 546, 100),
		Item_Base(2601, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 100),
		Item_Base(2602, LoC::Common, LoC::Support, LoC::SlotFace, 546, 100),
		Item_Base(2603, LoC::Common, LoC::Support, LoC::SlotHead, 546, 100),
		Item_Base(2604, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 100),
		Item_Base(2605, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 100),
		Item_Base(2606, LoC::Common, LoC::Support, LoC::SlotArms, 546, 100),
		Item_Base(2607, LoC::Common, LoC::Support, LoC::SlotHand, 546, 100),
		Item_Base(2608, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 100),
		Item_Base(2609, LoC::Common, LoC::Support, LoC::SlotChest, 546, 100),
		Item_Base(2610, LoC::Common, LoC::Support, LoC::SlotBack, 546, 100),
		Item_Base(2611, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 100),
		Item_Base(2612, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 100),
		Item_Base(2613, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 100),
		Item_Base(2614, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 100),
		Item_Base(2615, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 100),
		Item_Base(2616, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 100),
		Item_Base(2617, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 100),
		Item_Base(2618, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 100),
		Item_Base(2619, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 100),
		Item_Base(2620, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 100),
		Item_Base(2621, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 100),
		Item_Base(2622, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 100),
		Item_Base(2623, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 100),
		Item_Base(2624, LoC::Common, LoC::Support, LoC::SlotShield, 546, 100),
		Item_Base(2625, LoC::Common, LoC::Support, LoC::SlotEar, 546, 110),
		Item_Base(2626, LoC::Common, LoC::Support, LoC::SlotNeck, 546, 110),
		Item_Base(2627, LoC::Common, LoC::Support, LoC::SlotFace, 546, 110),
		Item_Base(2628, LoC::Common, LoC::Support, LoC::SlotHead, 546, 110),
		Item_Base(2629, LoC::Common, LoC::Support, LoC::SlotFingers, 546, 110),
		Item_Base(2630, LoC::Common, LoC::Support, LoC::SlotWrist, 546, 110),
		Item_Base(2631, LoC::Common, LoC::Support, LoC::SlotArms, 546, 110),
		Item_Base(2632, LoC::Common, LoC::Support, LoC::SlotHand, 546, 110),
		Item_Base(2633, LoC::Common, LoC::Support, LoC::SlotShoulders, 546, 110),
		Item_Base(2634, LoC::Common, LoC::Support, LoC::SlotChest, 546, 110),
		Item_Base(2635, LoC::Common, LoC::Support, LoC::SlotBack, 546, 110),
		Item_Base(2636, LoC::Common, LoC::Support, LoC::SlotWaist, 546, 110),
		Item_Base(2637, LoC::Common, LoC::Support, LoC::SlotLegs, 546, 110),
		Item_Base(2638, LoC::Common, LoC::Support, LoC::SlotFeet, 546, 110),
		Item_Base(2639, LoC::Common, LoC::Support, LoC::SlotCharm, 546, 110),
		Item_Base(2640, LoC::Common, LoC::Support, LoC::SlotPowerSource, 546, 110),
		Item_Base(2641, LoC::Common, LoC::Support, LoC::SlotOneHB, 546, 110),
		Item_Base(2642, LoC::Common, LoC::Support, LoC::SlotTwoHB, 546, 110),
		Item_Base(2643, LoC::Common, LoC::Support, LoC::SlotOneHS, 546, 110),
		Item_Base(2644, LoC::Common, LoC::Support, LoC::SlotTwoHS, 546, 110),
		Item_Base(2645, LoC::Common, LoC::Support, LoC::SlotArchery, 546, 110),
		Item_Base(2646, LoC::Common, LoC::Support, LoC::SlotThrowing, 546, 110),
		Item_Base(2647, LoC::Common, LoC::Support, LoC::SlotOneHP, 546, 110),
		Item_Base(2648, LoC::Common, LoC::Support, LoC::SlotTwoHP, 546, 110),
		Item_Base(2649, LoC::Common, LoC::Support, LoC::SlotShield, 546, 110),
		Item_Base(2650, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 10),
		Item_Base(2651, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 10),
		Item_Base(2652, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 10),
		Item_Base(2653, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 10),
		Item_Base(2654, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 10),
		Item_Base(2655, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 10),
		Item_Base(2656, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 10),
		Item_Base(2657, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 10),
		Item_Base(2658, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 10),
		Item_Base(2659, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 10),
		Item_Base(2660, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 10),
		Item_Base(2661, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 10),
		Item_Base(2662, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 10),
		Item_Base(2663, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 10),
		Item_Base(2664, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 10),
		Item_Base(2665, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 10),
		Item_Base(2666, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 10),
		Item_Base(2667, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 10),
		Item_Base(2668, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 10),
		Item_Base(2669, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 10),
		Item_Base(2670, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 10),
		Item_Base(2671, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 10),
		Item_Base(2672, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 10),
		Item_Base(2673, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 10),
		Item_Base(2674, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 10),
		Item_Base(2675, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 20),
		Item_Base(2676, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 20),
		Item_Base(2677, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 20),
		Item_Base(2678, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 20),
		Item_Base(2679, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 20),
		Item_Base(2680, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 20),
		Item_Base(2681, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 20),
		Item_Base(2682, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 20),
		Item_Base(2683, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 20),
		Item_Base(2684, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 20),
		Item_Base(2685, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 20),
		Item_Base(2686, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 20),
		Item_Base(2687, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 20),
		Item_Base(2688, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 20),
		Item_Base(2689, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 20),
		Item_Base(2690, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 20),
		Item_Base(2691, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 20),
		Item_Base(2692, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 20),
		Item_Base(2693, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 20),
		Item_Base(2694, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 20),
		Item_Base(2695, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 20),
		Item_Base(2696, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 20),
		Item_Base(2697, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 20),
		Item_Base(2698, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 20),
		Item_Base(2699, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 20),
		Item_Base(2700, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 30),
		Item_Base(2701, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 30),
		Item_Base(2702, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 30),
		Item_Base(2703, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 30),
		Item_Base(2704, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 30),
		Item_Base(2705, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 30),
		Item_Base(2706, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 30),
		Item_Base(2707, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 30),
		Item_Base(2708, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 30),
		Item_Base(2709, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 30),
		Item_Base(2710, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 30),
		Item_Base(2711, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 30),
		Item_Base(2712, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 30),
		Item_Base(2713, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 30),
		Item_Base(2714, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 30),
		Item_Base(2715, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 30),
		Item_Base(2716, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 30),
		Item_Base(2717, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 30),
		Item_Base(2718, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 30),
		Item_Base(2719, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 30),
		Item_Base(2720, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 30),
		Item_Base(2721, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 30),
		Item_Base(2722, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 30),
		Item_Base(2723, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 30),
		Item_Base(2724, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 30),
		Item_Base(2725, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 40),
		Item_Base(2726, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 40),
		Item_Base(2727, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 40),
		Item_Base(2728, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 40),
		Item_Base(2729, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 40),
		Item_Base(2730, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 40),
		Item_Base(2731, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 40),
		Item_Base(2732, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 40),
		Item_Base(2733, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 40),
		Item_Base(2734, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 40),
		Item_Base(2735, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 40),
		Item_Base(2736, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 40),
		Item_Base(2737, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 40),
		Item_Base(2738, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 40),
		Item_Base(2739, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 40),
		Item_Base(2740, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 40),
		Item_Base(2741, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 40),
		Item_Base(2742, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 40),
		Item_Base(2743, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 40),
		Item_Base(2744, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 40),
		Item_Base(2745, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 40),
		Item_Base(2746, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 40),
		Item_Base(2747, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 40),
		Item_Base(2748, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 40),
		Item_Base(2749, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 40),
		Item_Base(2750, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 50),
		Item_Base(2751, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 50),
		Item_Base(2752, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 50),
		Item_Base(2753, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 50),
		Item_Base(2754, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 50),
		Item_Base(2755, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 50),
		Item_Base(2756, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 50),
		Item_Base(2757, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 50),
		Item_Base(2758, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 50),
		Item_Base(2759, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 50),
		Item_Base(2760, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 50),
		Item_Base(2761, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 50),
		Item_Base(2762, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 50),
		Item_Base(2763, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 50),
		Item_Base(2764, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 50),
		Item_Base(2765, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 50),
		Item_Base(2766, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 50),
		Item_Base(2767, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 50),
		Item_Base(2768, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 50),
		Item_Base(2769, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 50),
		Item_Base(2770, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 50),
		Item_Base(2771, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 50),
		Item_Base(2772, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 50),
		Item_Base(2773, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 50),
		Item_Base(2774, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 50),
		Item_Base(2775, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 60),
		Item_Base(2776, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 60),
		Item_Base(2777, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 60),
		Item_Base(2778, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 60),
		Item_Base(2779, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 60),
		Item_Base(2780, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 60),
		Item_Base(2781, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 60),
		Item_Base(2782, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 60),
		Item_Base(2783, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 60),
		Item_Base(2784, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 60),
		Item_Base(2785, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 60),
		Item_Base(2786, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 60),
		Item_Base(2787, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 60),
		Item_Base(2788, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 60),
		Item_Base(2789, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 60),
		Item_Base(2790, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 60),
		Item_Base(2791, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 60),
		Item_Base(2792, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 60),
		Item_Base(2793, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 60),
		Item_Base(2794, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 60),
		Item_Base(2795, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 60),
		Item_Base(2796, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 60),
		Item_Base(2797, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 60),
		Item_Base(2798, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 60),
		Item_Base(2799, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 60),
		Item_Base(2800, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 70),
		Item_Base(2801, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 70),
		Item_Base(2802, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 70),
		Item_Base(2803, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 70),
		Item_Base(2804, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 70),
		Item_Base(2805, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 70),
		Item_Base(2806, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 70),
		Item_Base(2807, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 70),
		Item_Base(2808, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 70),
		Item_Base(2809, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 70),
		Item_Base(2810, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 70),
		Item_Base(2811, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 70),
		Item_Base(2812, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 70),
		Item_Base(2813, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 70),
		Item_Base(2814, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 70),
		Item_Base(2815, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 70),
		Item_Base(2816, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 70),
		Item_Base(2817, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 70),
		Item_Base(2818, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 70),
		Item_Base(2819, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 70),
		Item_Base(2820, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 70),
		Item_Base(2821, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 70),
		Item_Base(2822, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 70),
		Item_Base(2823, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 70),
		Item_Base(2824, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 70),
		Item_Base(2825, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 80),
		Item_Base(2826, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 80),
		Item_Base(2827, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 80),
		Item_Base(2828, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 80),
		Item_Base(2829, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 80),
		Item_Base(2830, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 80),
		Item_Base(2831, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 80),
		Item_Base(2832, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 80),
		Item_Base(2833, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 80),
		Item_Base(2834, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 80),
		Item_Base(2835, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 80),
		Item_Base(2836, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 80),
		Item_Base(2837, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 80),
		Item_Base(2838, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 80),
		Item_Base(2839, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 80),
		Item_Base(2840, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 80),
		Item_Base(2841, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 80),
		Item_Base(2842, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 80),
		Item_Base(2843, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 80),
		Item_Base(2844, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 80),
		Item_Base(2845, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 80),
		Item_Base(2846, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 80),
		Item_Base(2847, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 80),
		Item_Base(2848, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 80),
		Item_Base(2849, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 80),
		Item_Base(2850, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 90),
		Item_Base(2851, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 90),
		Item_Base(2852, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 90),
		Item_Base(2853, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 90),
		Item_Base(2854, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 90),
		Item_Base(2855, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 90),
		Item_Base(2856, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 90),
		Item_Base(2857, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 90),
		Item_Base(2858, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 90),
		Item_Base(2859, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 90),
		Item_Base(2860, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 90),
		Item_Base(2861, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 90),
		Item_Base(2862, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 90),
		Item_Base(2863, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 90),
		Item_Base(2864, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 90),
		Item_Base(2865, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 90),
		Item_Base(2866, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 90),
		Item_Base(2867, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 90),
		Item_Base(2868, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 90),
		Item_Base(2869, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 90),
		Item_Base(2870, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 90),
		Item_Base(2871, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 90),
		Item_Base(2872, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 90),
		Item_Base(2873, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 90),
		Item_Base(2874, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 90),
		Item_Base(2875, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 100),
		Item_Base(2876, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 100),
		Item_Base(2877, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 100),
		Item_Base(2878, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 100),
		Item_Base(2879, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 100),
		Item_Base(2880, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 100),
		Item_Base(2881, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 100),
		Item_Base(2882, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 100),
		Item_Base(2883, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 100),
		Item_Base(2884, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 100),
		Item_Base(2885, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 100),
		Item_Base(2886, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 100),
		Item_Base(2887, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 100),
		Item_Base(2888, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 100),
		Item_Base(2889, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 100),
		Item_Base(2890, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 100),
		Item_Base(2891, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 100),
		Item_Base(2892, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 100),
		Item_Base(2893, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 100),
		Item_Base(2894, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 100),
		Item_Base(2895, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 100),
		Item_Base(2896, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 100),
		Item_Base(2897, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 100),
		Item_Base(2898, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 100),
		Item_Base(2899, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 100),
		Item_Base(2900, LoC::Uncommon, LoC::Support, LoC::SlotEar, 546, 110),
		Item_Base(2901, LoC::Uncommon, LoC::Support, LoC::SlotNeck, 546, 110),
		Item_Base(2902, LoC::Uncommon, LoC::Support, LoC::SlotFace, 546, 110),
		Item_Base(2903, LoC::Uncommon, LoC::Support, LoC::SlotHead, 546, 110),
		Item_Base(2904, LoC::Uncommon, LoC::Support, LoC::SlotFingers, 546, 110),
		Item_Base(2905, LoC::Uncommon, LoC::Support, LoC::SlotWrist, 546, 110),
		Item_Base(2906, LoC::Uncommon, LoC::Support, LoC::SlotArms, 546, 110),
		Item_Base(2907, LoC::Uncommon, LoC::Support, LoC::SlotHand, 546, 110),
		Item_Base(2908, LoC::Uncommon, LoC::Support, LoC::SlotShoulders, 546, 110),
		Item_Base(2909, LoC::Uncommon, LoC::Support, LoC::SlotChest, 546, 110),
		Item_Base(2910, LoC::Uncommon, LoC::Support, LoC::SlotBack, 546, 110),
		Item_Base(2911, LoC::Uncommon, LoC::Support, LoC::SlotWaist, 546, 110),
		Item_Base(2912, LoC::Uncommon, LoC::Support, LoC::SlotLegs, 546, 110),
		Item_Base(2913, LoC::Uncommon, LoC::Support, LoC::SlotFeet, 546, 110),
		Item_Base(2914, LoC::Uncommon, LoC::Support, LoC::SlotCharm, 546, 110),
		Item_Base(2915, LoC::Uncommon, LoC::Support, LoC::SlotPowerSource, 546, 110),
		Item_Base(2916, LoC::Uncommon, LoC::Support, LoC::SlotOneHB, 546, 110),
		Item_Base(2917, LoC::Uncommon, LoC::Support, LoC::SlotTwoHB, 546, 110),
		Item_Base(2918, LoC::Uncommon, LoC::Support, LoC::SlotOneHS, 546, 110),
		Item_Base(2919, LoC::Uncommon, LoC::Support, LoC::SlotTwoHS, 546, 110),
		Item_Base(2920, LoC::Uncommon, LoC::Support, LoC::SlotArchery, 546, 110),
		Item_Base(2921, LoC::Uncommon, LoC::Support, LoC::SlotThrowing, 546, 110),
		Item_Base(2922, LoC::Uncommon, LoC::Support, LoC::SlotOneHP, 546, 110),
		Item_Base(2923, LoC::Uncommon, LoC::Support, LoC::SlotTwoHP, 546, 110),
		Item_Base(2924, LoC::Uncommon, LoC::Support, LoC::SlotShield, 546, 110),
		Item_Base(2925, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 10),
		Item_Base(2926, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 10),
		Item_Base(2927, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 10),
		Item_Base(2928, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 10),
		Item_Base(2929, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 10),
		Item_Base(2930, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 10),
		Item_Base(2931, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 10),
		Item_Base(2932, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 10),
		Item_Base(2933, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 10),
		Item_Base(2934, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 10),
		Item_Base(2935, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 10),
		Item_Base(2936, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 10),
		Item_Base(2937, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 10),
		Item_Base(2938, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 10),
		Item_Base(2939, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 10),
		Item_Base(2940, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 10),
		Item_Base(2941, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 10),
		Item_Base(2942, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 10),
		Item_Base(2943, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 10),
		Item_Base(2944, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 10),
		Item_Base(2945, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 10),
		Item_Base(2946, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 10),
		Item_Base(2947, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 10),
		Item_Base(2948, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 10),
		Item_Base(2949, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 10),
		Item_Base(2950, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 20),
		Item_Base(2951, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 20),
		Item_Base(2952, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 20),
		Item_Base(2953, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 20),
		Item_Base(2954, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 20),
		Item_Base(2955, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 20),
		Item_Base(2956, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 20),
		Item_Base(2957, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 20),
		Item_Base(2958, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 20),
		Item_Base(2959, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 20),
		Item_Base(2960, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 20),
		Item_Base(2961, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 20),
		Item_Base(2962, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 20),
		Item_Base(2963, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 20),
		Item_Base(2964, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 20),
		Item_Base(2965, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 20),
		Item_Base(2966, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 20),
		Item_Base(2967, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 20),
		Item_Base(2968, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 20),
		Item_Base(2969, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 20),
		Item_Base(2970, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 20),
		Item_Base(2971, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 20),
		Item_Base(2972, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 20),
		Item_Base(2973, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 20),
		Item_Base(2974, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 20),
		Item_Base(2975, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 30),
		Item_Base(2976, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 30),
		Item_Base(2977, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 30),
		Item_Base(2978, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 30),
		Item_Base(2979, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 30),
		Item_Base(2980, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 30),
		Item_Base(2981, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 30),
		Item_Base(2982, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 30),
		Item_Base(2983, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 30),
		Item_Base(2984, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 30),
		Item_Base(2985, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 30),
		Item_Base(2986, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 30),
		Item_Base(2987, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 30),
		Item_Base(2988, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 30),
		Item_Base(2989, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 30),
		Item_Base(2990, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 30),
		Item_Base(2991, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 30),
		Item_Base(2992, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 30),
		Item_Base(2993, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 30),
		Item_Base(2994, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 30),
		Item_Base(2995, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 30),
		Item_Base(2996, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 30),
		Item_Base(2997, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 30),
		Item_Base(2998, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 30),
		Item_Base(2999, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 30),
		Item_Base(3000, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 40),
		Item_Base(3001, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 40),
		Item_Base(3002, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 40),
		Item_Base(3003, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 40),
		Item_Base(3004, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 40),
		Item_Base(3005, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 40),
		Item_Base(3006, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 40),
		Item_Base(3007, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 40),
		Item_Base(3008, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 40),
		Item_Base(3009, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 40),
		Item_Base(3010, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 40),
		Item_Base(3011, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 40),
		Item_Base(3012, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 40),
		Item_Base(3013, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 40),
		Item_Base(3014, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 40),
		Item_Base(3015, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 40),
		Item_Base(3016, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 40),
		Item_Base(3017, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 40),
		Item_Base(3018, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 40),
		Item_Base(3019, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 40),
		Item_Base(3020, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 40),
		Item_Base(3021, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 40),
		Item_Base(3022, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 40),
		Item_Base(3023, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 40),
		Item_Base(3024, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 40),
		Item_Base(3025, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 50),
		Item_Base(3026, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 50),
		Item_Base(3027, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 50),
		Item_Base(3028, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 50),
		Item_Base(3029, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 50),
		Item_Base(3030, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 50),
		Item_Base(3031, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 50),
		Item_Base(3032, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 50),
		Item_Base(3033, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 50),
		Item_Base(3034, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 50),
		Item_Base(3035, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 50),
		Item_Base(3036, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 50),
		Item_Base(3037, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 50),
		Item_Base(3038, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 50),
		Item_Base(3039, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 50),
		Item_Base(3040, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 50),
		Item_Base(3041, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 50),
		Item_Base(3042, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 50),
		Item_Base(3043, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 50),
		Item_Base(3044, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 50),
		Item_Base(3045, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 50),
		Item_Base(3046, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 50),
		Item_Base(3047, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 50),
		Item_Base(3048, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 50),
		Item_Base(3049, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 50),
		Item_Base(3050, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 60),
		Item_Base(3051, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 60),
		Item_Base(3052, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 60),
		Item_Base(3053, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 60),
		Item_Base(3054, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 60),
		Item_Base(3055, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 60),
		Item_Base(3056, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 60),
		Item_Base(3057, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 60),
		Item_Base(3058, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 60),
		Item_Base(3059, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 60),
		Item_Base(3060, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 60),
		Item_Base(3061, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 60),
		Item_Base(3062, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 60),
		Item_Base(3063, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 60),
		Item_Base(3064, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 60),
		Item_Base(3065, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 60),
		Item_Base(3066, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 60),
		Item_Base(3067, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 60),
		Item_Base(3068, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 60),
		Item_Base(3069, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 60),
		Item_Base(3070, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 60),
		Item_Base(3071, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 60),
		Item_Base(3072, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 60),
		Item_Base(3073, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 60),
		Item_Base(3074, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 60),
		Item_Base(3075, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 70),
		Item_Base(3076, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 70),
		Item_Base(3077, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 70),
		Item_Base(3078, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 70),
		Item_Base(3079, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 70),
		Item_Base(3080, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 70),
		Item_Base(3081, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 70),
		Item_Base(3082, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 70),
		Item_Base(3083, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 70),
		Item_Base(3084, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 70),
		Item_Base(3085, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 70),
		Item_Base(3086, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 70),
		Item_Base(3087, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 70),
		Item_Base(3088, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 70),
		Item_Base(3089, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 70),
		Item_Base(3090, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 70),
		Item_Base(3091, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 70),
		Item_Base(3092, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 70),
		Item_Base(3093, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 70),
		Item_Base(3094, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 70),
		Item_Base(3095, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 70),
		Item_Base(3096, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 70),
		Item_Base(3097, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 70),
		Item_Base(3098, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 70),
		Item_Base(3099, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 70),
		Item_Base(3100, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 80),
		Item_Base(3101, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 80),
		Item_Base(3102, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 80),
		Item_Base(3103, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 80),
		Item_Base(3104, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 80),
		Item_Base(3105, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 80),
		Item_Base(3106, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 80),
		Item_Base(3107, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 80),
		Item_Base(3108, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 80),
		Item_Base(3109, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 80),
		Item_Base(3110, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 80),
		Item_Base(3111, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 80),
		Item_Base(3112, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 80),
		Item_Base(3113, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 80),
		Item_Base(3114, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 80),
		Item_Base(3115, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 80),
		Item_Base(3116, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 80),
		Item_Base(3117, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 80),
		Item_Base(3118, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 80),
		Item_Base(3119, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 80),
		Item_Base(3120, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 80),
		Item_Base(3121, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 80),
		Item_Base(3122, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 80),
		Item_Base(3123, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 80),
		Item_Base(3124, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 80),
		Item_Base(3125, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 90),
		Item_Base(3126, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 90),
		Item_Base(3127, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 90),
		Item_Base(3128, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 90),
		Item_Base(3129, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 90),
		Item_Base(3130, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 90),
		Item_Base(3131, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 90),
		Item_Base(3132, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 90),
		Item_Base(3133, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 90),
		Item_Base(3134, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 90),
		Item_Base(3135, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 90),
		Item_Base(3136, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 90),
		Item_Base(3137, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 90),
		Item_Base(3138, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 90),
		Item_Base(3139, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 90),
		Item_Base(3140, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 90),
		Item_Base(3141, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 90),
		Item_Base(3142, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 90),
		Item_Base(3143, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 90),
		Item_Base(3144, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 90),
		Item_Base(3145, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 90),
		Item_Base(3146, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 90),
		Item_Base(3147, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 90),
		Item_Base(3148, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 90),
		Item_Base(3149, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 90),
		Item_Base(3150, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 100),
		Item_Base(3151, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 100),
		Item_Base(3152, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 100),
		Item_Base(3153, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 100),
		Item_Base(3154, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 100),
		Item_Base(3155, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 100),
		Item_Base(3156, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 100),
		Item_Base(3157, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 100),
		Item_Base(3158, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 100),
		Item_Base(3159, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 100),
		Item_Base(3160, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 100),
		Item_Base(3161, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 100),
		Item_Base(3162, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 100),
		Item_Base(3163, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 100),
		Item_Base(3164, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 100),
		Item_Base(3165, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 100),
		Item_Base(3166, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 100),
		Item_Base(3167, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 100),
		Item_Base(3168, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 100),
		Item_Base(3169, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 100),
		Item_Base(3170, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 100),
		Item_Base(3171, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 100),
		Item_Base(3172, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 100),
		Item_Base(3173, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 100),
		Item_Base(3174, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 100),
		Item_Base(3175, LoC::Rare, LoC::Support, LoC::SlotEar, 546, 110),
		Item_Base(3176, LoC::Rare, LoC::Support, LoC::SlotNeck, 546, 110),
		Item_Base(3177, LoC::Rare, LoC::Support, LoC::SlotFace, 546, 110),
		Item_Base(3178, LoC::Rare, LoC::Support, LoC::SlotHead, 546, 110),
		Item_Base(3179, LoC::Rare, LoC::Support, LoC::SlotFingers, 546, 110),
		Item_Base(3180, LoC::Rare, LoC::Support, LoC::SlotWrist, 546, 110),
		Item_Base(3181, LoC::Rare, LoC::Support, LoC::SlotArms, 546, 110),
		Item_Base(3182, LoC::Rare, LoC::Support, LoC::SlotHand, 546, 110),
		Item_Base(3183, LoC::Rare, LoC::Support, LoC::SlotShoulders, 546, 110),
		Item_Base(3184, LoC::Rare, LoC::Support, LoC::SlotChest, 546, 110),
		Item_Base(3185, LoC::Rare, LoC::Support, LoC::SlotBack, 546, 110),
		Item_Base(3186, LoC::Rare, LoC::Support, LoC::SlotWaist, 546, 110),
		Item_Base(3187, LoC::Rare, LoC::Support, LoC::SlotLegs, 546, 110),
		Item_Base(3188, LoC::Rare, LoC::Support, LoC::SlotFeet, 546, 110),
		Item_Base(3189, LoC::Rare, LoC::Support, LoC::SlotCharm, 546, 110),
		Item_Base(3190, LoC::Rare, LoC::Support, LoC::SlotPowerSource, 546, 110),
		Item_Base(3191, LoC::Rare, LoC::Support, LoC::SlotOneHB, 546, 110),
		Item_Base(3192, LoC::Rare, LoC::Support, LoC::SlotTwoHB, 546, 110),
		Item_Base(3193, LoC::Rare, LoC::Support, LoC::SlotOneHS, 546, 110),
		Item_Base(3194, LoC::Rare, LoC::Support, LoC::SlotTwoHS, 546, 110),
		Item_Base(3195, LoC::Rare, LoC::Support, LoC::SlotArchery, 546, 110),
		Item_Base(3196, LoC::Rare, LoC::Support, LoC::SlotThrowing, 546, 110),
		Item_Base(3197, LoC::Rare, LoC::Support, LoC::SlotOneHP, 546, 110),
		Item_Base(3198, LoC::Rare, LoC::Support, LoC::SlotTwoHP, 546, 110),
		Item_Base(3199, LoC::Rare, LoC::Support, LoC::SlotShield, 546, 110),
		Item_Base(3200, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 10),
		Item_Base(3201, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 10),
		Item_Base(3202, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 10),
		Item_Base(3203, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 10),
		Item_Base(3204, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 10),
		Item_Base(3205, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 10),
		Item_Base(3206, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 10),
		Item_Base(3207, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 10),
		Item_Base(3208, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 10),
		Item_Base(3209, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 10),
		Item_Base(3210, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 10),
		Item_Base(3211, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 10),
		Item_Base(3212, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 10),
		Item_Base(3213, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 10),
		Item_Base(3214, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 10),
		Item_Base(3215, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 10),
		Item_Base(3216, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 10),
		Item_Base(3217, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 10),
		Item_Base(3218, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 10),
		Item_Base(3219, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 10),
		Item_Base(3220, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 10),
		Item_Base(3221, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 10),
		Item_Base(3222, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 10),
		Item_Base(3223, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 10),
		Item_Base(3224, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 10),
		Item_Base(3225, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 20),
		Item_Base(3226, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 20),
		Item_Base(3227, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 20),
		Item_Base(3228, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 20),
		Item_Base(3229, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 20),
		Item_Base(3230, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 20),
		Item_Base(3231, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 20),
		Item_Base(3232, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 20),
		Item_Base(3233, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 20),
		Item_Base(3234, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 20),
		Item_Base(3235, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 20),
		Item_Base(3236, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 20),
		Item_Base(3237, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 20),
		Item_Base(3238, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 20),
		Item_Base(3239, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 20),
		Item_Base(3240, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 20),
		Item_Base(3241, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 20),
		Item_Base(3242, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 20),
		Item_Base(3243, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 20),
		Item_Base(3244, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 20),
		Item_Base(3245, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 20),
		Item_Base(3246, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 20),
		Item_Base(3247, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 20),
		Item_Base(3248, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 20),
		Item_Base(3249, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 20),
		Item_Base(3250, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 30),
		Item_Base(3251, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 30),
		Item_Base(3252, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 30),
		Item_Base(3253, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 30),
		Item_Base(3254, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 30),
		Item_Base(3255, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 30),
		Item_Base(3256, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 30),
		Item_Base(3257, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 30),
		Item_Base(3258, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 30),
		Item_Base(3259, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 30),
		Item_Base(3260, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 30),
		Item_Base(3261, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 30),
		Item_Base(3262, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 30),
		Item_Base(3263, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 30),
		Item_Base(3264, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 30),
		Item_Base(3265, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 30),
		Item_Base(3266, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 30),
		Item_Base(3267, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 30),
		Item_Base(3268, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 30),
		Item_Base(3269, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 30),
		Item_Base(3270, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 30),
		Item_Base(3271, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 30),
		Item_Base(3272, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 30),
		Item_Base(3273, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 30),
		Item_Base(3274, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 30),
		Item_Base(3275, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 40),
		Item_Base(3276, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 40),
		Item_Base(3277, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 40),
		Item_Base(3278, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 40),
		Item_Base(3279, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 40),
		Item_Base(3280, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 40),
		Item_Base(3281, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 40),
		Item_Base(3282, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 40),
		Item_Base(3283, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 40),
		Item_Base(3284, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 40),
		Item_Base(3285, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 40),
		Item_Base(3286, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 40),
		Item_Base(3287, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 40),
		Item_Base(3288, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 40),
		Item_Base(3289, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 40),
		Item_Base(3290, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 40),
		Item_Base(3291, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 40),
		Item_Base(3292, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 40),
		Item_Base(3293, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 40),
		Item_Base(3294, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 40),
		Item_Base(3295, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 40),
		Item_Base(3296, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 40),
		Item_Base(3297, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 40),
		Item_Base(3298, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 40),
		Item_Base(3299, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 40),
		Item_Base(3300, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 50),
		Item_Base(3301, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 50),
		Item_Base(3302, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 50),
		Item_Base(3303, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 50),
		Item_Base(3304, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 50),
		Item_Base(3305, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 50),
		Item_Base(3306, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 50),
		Item_Base(3307, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 50),
		Item_Base(3308, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 50),
		Item_Base(3309, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 50),
		Item_Base(3310, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 50),
		Item_Base(3311, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 50),
		Item_Base(3312, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 50),
		Item_Base(3313, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 50),
		Item_Base(3314, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 50),
		Item_Base(3315, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 50),
		Item_Base(3316, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 50),
		Item_Base(3317, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 50),
		Item_Base(3318, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 50),
		Item_Base(3319, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 50),
		Item_Base(3320, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 50),
		Item_Base(3321, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 50),
		Item_Base(3322, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 50),
		Item_Base(3323, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 50),
		Item_Base(3324, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 50),
		Item_Base(3325, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 60),
		Item_Base(3326, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 60),
		Item_Base(3327, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 60),
		Item_Base(3328, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 60),
		Item_Base(3329, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 60),
		Item_Base(3330, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 60),
		Item_Base(3331, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 60),
		Item_Base(3332, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 60),
		Item_Base(3333, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 60),
		Item_Base(3334, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 60),
		Item_Base(3335, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 60),
		Item_Base(3336, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 60),
		Item_Base(3337, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 60),
		Item_Base(3338, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 60),
		Item_Base(3339, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 60),
		Item_Base(3340, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 60),
		Item_Base(3341, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 60),
		Item_Base(3342, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 60),
		Item_Base(3343, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 60),
		Item_Base(3344, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 60),
		Item_Base(3345, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 60),
		Item_Base(3346, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 60),
		Item_Base(3347, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 60),
		Item_Base(3348, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 60),
		Item_Base(3349, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 60),
		Item_Base(3350, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 70),
		Item_Base(3351, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 70),
		Item_Base(3352, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 70),
		Item_Base(3353, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 70),
		Item_Base(3354, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 70),
		Item_Base(3355, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 70),
		Item_Base(3356, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 70),
		Item_Base(3357, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 70),
		Item_Base(3358, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 70),
		Item_Base(3359, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 70),
		Item_Base(3360, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 70),
		Item_Base(3361, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 70),
		Item_Base(3362, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 70),
		Item_Base(3363, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 70),
		Item_Base(3364, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 70),
		Item_Base(3365, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 70),
		Item_Base(3366, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 70),
		Item_Base(3367, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 70),
		Item_Base(3368, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 70),
		Item_Base(3369, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 70),
		Item_Base(3370, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 70),
		Item_Base(3371, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 70),
		Item_Base(3372, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 70),
		Item_Base(3373, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 70),
		Item_Base(3374, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 70),
		Item_Base(3375, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 80),
		Item_Base(3376, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 80),
		Item_Base(3377, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 80),
		Item_Base(3378, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 80),
		Item_Base(3379, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 80),
		Item_Base(3380, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 80),
		Item_Base(3381, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 80),
		Item_Base(3382, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 80),
		Item_Base(3383, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 80),
		Item_Base(3384, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 80),
		Item_Base(3385, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 80),
		Item_Base(3386, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 80),
		Item_Base(3387, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 80),
		Item_Base(3388, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 80),
		Item_Base(3389, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 80),
		Item_Base(3390, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 80),
		Item_Base(3391, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 80),
		Item_Base(3392, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 80),
		Item_Base(3393, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 80),
		Item_Base(3394, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 80),
		Item_Base(3395, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 80),
		Item_Base(3396, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 80),
		Item_Base(3397, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 80),
		Item_Base(3398, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 80),
		Item_Base(3399, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 80),
		Item_Base(3400, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 90),
		Item_Base(3401, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 90),
		Item_Base(3402, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 90),
		Item_Base(3403, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 90),
		Item_Base(3404, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 90),
		Item_Base(3405, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 90),
		Item_Base(3406, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 90),
		Item_Base(3407, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 90),
		Item_Base(3408, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 90),
		Item_Base(3409, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 90),
		Item_Base(3410, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 90),
		Item_Base(3411, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 90),
		Item_Base(3412, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 90),
		Item_Base(3413, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 90),
		Item_Base(3414, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 90),
		Item_Base(3415, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 90),
		Item_Base(3416, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 90),
		Item_Base(3417, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 90),
		Item_Base(3418, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 90),
		Item_Base(3419, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 90),
		Item_Base(3420, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 90),
		Item_Base(3421, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 90),
		Item_Base(3422, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 90),
		Item_Base(3423, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 90),
		Item_Base(3424, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 90),
		Item_Base(3425, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 100),
		Item_Base(3426, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 100),
		Item_Base(3427, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 100),
		Item_Base(3428, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 100),
		Item_Base(3429, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 100),
		Item_Base(3430, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 100),
		Item_Base(3431, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 100),
		Item_Base(3432, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 100),
		Item_Base(3433, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 100),
		Item_Base(3434, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 100),
		Item_Base(3435, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 100),
		Item_Base(3436, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 100),
		Item_Base(3437, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 100),
		Item_Base(3438, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 100),
		Item_Base(3439, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 100),
		Item_Base(3440, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 100),
		Item_Base(3441, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 100),
		Item_Base(3442, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 100),
		Item_Base(3443, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 100),
		Item_Base(3444, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 100),
		Item_Base(3445, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 100),
		Item_Base(3446, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 100),
		Item_Base(3447, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 100),
		Item_Base(3448, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 100),
		Item_Base(3449, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 100),
		Item_Base(3450, LoC::Legendary, LoC::Support, LoC::SlotEar, 546, 110),
		Item_Base(3451, LoC::Legendary, LoC::Support, LoC::SlotNeck, 546, 110),
		Item_Base(3452, LoC::Legendary, LoC::Support, LoC::SlotFace, 546, 110),
		Item_Base(3453, LoC::Legendary, LoC::Support, LoC::SlotHead, 546, 110),
		Item_Base(3454, LoC::Legendary, LoC::Support, LoC::SlotFingers, 546, 110),
		Item_Base(3455, LoC::Legendary, LoC::Support, LoC::SlotWrist, 546, 110),
		Item_Base(3456, LoC::Legendary, LoC::Support, LoC::SlotArms, 546, 110),
		Item_Base(3457, LoC::Legendary, LoC::Support, LoC::SlotHand, 546, 110),
		Item_Base(3458, LoC::Legendary, LoC::Support, LoC::SlotShoulders, 546, 110),
		Item_Base(3459, LoC::Legendary, LoC::Support, LoC::SlotChest, 546, 110),
		Item_Base(3460, LoC::Legendary, LoC::Support, LoC::SlotBack, 546, 110),
		Item_Base(3461, LoC::Legendary, LoC::Support, LoC::SlotWaist, 546, 110),
		Item_Base(3462, LoC::Legendary, LoC::Support, LoC::SlotLegs, 546, 110),
		Item_Base(3463, LoC::Legendary, LoC::Support, LoC::SlotFeet, 546, 110),
		Item_Base(3464, LoC::Legendary, LoC::Support, LoC::SlotCharm, 546, 110),
		Item_Base(3465, LoC::Legendary, LoC::Support, LoC::SlotPowerSource, 546, 110),
		Item_Base(3466, LoC::Legendary, LoC::Support, LoC::SlotOneHB, 546, 110),
		Item_Base(3467, LoC::Legendary, LoC::Support, LoC::SlotTwoHB, 546, 110),
		Item_Base(3468, LoC::Legendary, LoC::Support, LoC::SlotOneHS, 546, 110),
		Item_Base(3469, LoC::Legendary, LoC::Support, LoC::SlotTwoHS, 546, 110),
		Item_Base(3470, LoC::Legendary, LoC::Support, LoC::SlotArchery, 546, 110),
		Item_Base(3471, LoC::Legendary, LoC::Support, LoC::SlotThrowing, 546, 110),
		Item_Base(3472, LoC::Legendary, LoC::Support, LoC::SlotOneHP, 546, 110),
		Item_Base(3473, LoC::Legendary, LoC::Support, LoC::SlotTwoHP, 546, 110),
		Item_Base(3474, LoC::Legendary, LoC::Support, LoC::SlotShield, 546, 110),
		Item_Base(3475, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 10),
		Item_Base(3476, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 10),
		Item_Base(3477, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 10),
		Item_Base(3478, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 10),
		Item_Base(3479, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 10),
		Item_Base(3480, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 10),
		Item_Base(3481, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 10),
		Item_Base(3482, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 10),
		Item_Base(3483, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 10),
		Item_Base(3484, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 10),
		Item_Base(3485, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 10),
		Item_Base(3486, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 10),
		Item_Base(3487, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 10),
		Item_Base(3488, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 10),
		Item_Base(3489, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 10),
		Item_Base(3490, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 10),
		Item_Base(3491, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 10),
		Item_Base(3492, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 10),
		Item_Base(3493, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 10),
		Item_Base(3494, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 10),
		Item_Base(3495, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 10),
		Item_Base(3496, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 10),
		Item_Base(3497, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 10),
		Item_Base(3498, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 10),
		Item_Base(3499, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 10),
		Item_Base(3500, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 20),
		Item_Base(3501, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 20),
		Item_Base(3502, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 20),
		Item_Base(3503, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 20),
		Item_Base(3504, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 20),
		Item_Base(3505, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 20),
		Item_Base(3506, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 20),
		Item_Base(3507, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 20),
		Item_Base(3508, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 20),
		Item_Base(3509, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 20),
		Item_Base(3510, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 20),
		Item_Base(3511, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 20),
		Item_Base(3512, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 20),
		Item_Base(3513, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 20),
		Item_Base(3514, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 20),
		Item_Base(3515, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 20),
		Item_Base(3516, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 20),
		Item_Base(3517, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 20),
		Item_Base(3518, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 20),
		Item_Base(3519, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 20),
		Item_Base(3520, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 20),
		Item_Base(3521, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 20),
		Item_Base(3522, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 20),
		Item_Base(3523, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 20),
		Item_Base(3524, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 20),
		Item_Base(3525, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 30),
		Item_Base(3526, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 30),
		Item_Base(3527, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 30),
		Item_Base(3528, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 30),
		Item_Base(3529, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 30),
		Item_Base(3530, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 30),
		Item_Base(3531, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 30),
		Item_Base(3532, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 30),
		Item_Base(3533, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 30),
		Item_Base(3534, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 30),
		Item_Base(3535, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 30),
		Item_Base(3536, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 30),
		Item_Base(3537, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 30),
		Item_Base(3538, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 30),
		Item_Base(3539, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 30),
		Item_Base(3540, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 30),
		Item_Base(3541, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 30),
		Item_Base(3542, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 30),
		Item_Base(3543, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 30),
		Item_Base(3544, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 30),
		Item_Base(3545, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 30),
		Item_Base(3546, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 30),
		Item_Base(3547, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 30),
		Item_Base(3548, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 30),
		Item_Base(3549, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 30),
		Item_Base(3550, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 40),
		Item_Base(3551, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 40),
		Item_Base(3552, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 40),
		Item_Base(3553, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 40),
		Item_Base(3554, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 40),
		Item_Base(3555, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 40),
		Item_Base(3556, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 40),
		Item_Base(3557, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 40),
		Item_Base(3558, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 40),
		Item_Base(3559, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 40),
		Item_Base(3560, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 40),
		Item_Base(3561, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 40),
		Item_Base(3562, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 40),
		Item_Base(3563, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 40),
		Item_Base(3564, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 40),
		Item_Base(3565, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 40),
		Item_Base(3566, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 40),
		Item_Base(3567, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 40),
		Item_Base(3568, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 40),
		Item_Base(3569, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 40),
		Item_Base(3570, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 40),
		Item_Base(3571, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 40),
		Item_Base(3572, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 40),
		Item_Base(3573, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 40),
		Item_Base(3574, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 40),
		Item_Base(3575, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 50),
		Item_Base(3576, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 50),
		Item_Base(3577, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 50),
		Item_Base(3578, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 50),
		Item_Base(3579, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 50),
		Item_Base(3580, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 50),
		Item_Base(3581, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 50),
		Item_Base(3582, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 50),
		Item_Base(3583, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 50),
		Item_Base(3584, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 50),
		Item_Base(3585, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 50),
		Item_Base(3586, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 50),
		Item_Base(3587, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 50),
		Item_Base(3588, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 50),
		Item_Base(3589, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 50),
		Item_Base(3590, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 50),
		Item_Base(3591, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 50),
		Item_Base(3592, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 50),
		Item_Base(3593, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 50),
		Item_Base(3594, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 50),
		Item_Base(3595, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 50),
		Item_Base(3596, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 50),
		Item_Base(3597, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 50),
		Item_Base(3598, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 50),
		Item_Base(3599, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 50),
		Item_Base(3600, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 60),
		Item_Base(3601, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 60),
		Item_Base(3602, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 60),
		Item_Base(3603, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 60),
		Item_Base(3604, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 60),
		Item_Base(3605, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 60),
		Item_Base(3606, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 60),
		Item_Base(3607, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 60),
		Item_Base(3608, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 60),
		Item_Base(3609, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 60),
		Item_Base(3610, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 60),
		Item_Base(3611, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 60),
		Item_Base(3612, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 60),
		Item_Base(3613, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 60),
		Item_Base(3614, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 60),
		Item_Base(3615, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 60),
		Item_Base(3616, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 60),
		Item_Base(3617, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 60),
		Item_Base(3618, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 60),
		Item_Base(3619, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 60),
		Item_Base(3620, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 60),
		Item_Base(3621, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 60),
		Item_Base(3622, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 60),
		Item_Base(3623, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 60),
		Item_Base(3624, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 60),
		Item_Base(3625, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 70),
		Item_Base(3626, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 70),
		Item_Base(3627, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 70),
		Item_Base(3628, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 70),
		Item_Base(3629, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 70),
		Item_Base(3630, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 70),
		Item_Base(3631, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 70),
		Item_Base(3632, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 70),
		Item_Base(3633, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 70),
		Item_Base(3634, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 70),
		Item_Base(3635, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 70),
		Item_Base(3636, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 70),
		Item_Base(3637, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 70),
		Item_Base(3638, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 70),
		Item_Base(3639, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 70),
		Item_Base(3640, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 70),
		Item_Base(3641, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 70),
		Item_Base(3642, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 70),
		Item_Base(3643, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 70),
		Item_Base(3644, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 70),
		Item_Base(3645, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 70),
		Item_Base(3646, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 70),
		Item_Base(3647, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 70),
		Item_Base(3648, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 70),
		Item_Base(3649, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 70),
		Item_Base(3650, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 80),
		Item_Base(3651, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 80),
		Item_Base(3652, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 80),
		Item_Base(3653, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 80),
		Item_Base(3654, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 80),
		Item_Base(3655, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 80),
		Item_Base(3656, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 80),
		Item_Base(3657, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 80),
		Item_Base(3658, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 80),
		Item_Base(3659, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 80),
		Item_Base(3660, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 80),
		Item_Base(3661, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 80),
		Item_Base(3662, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 80),
		Item_Base(3663, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 80),
		Item_Base(3664, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 80),
		Item_Base(3665, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 80),
		Item_Base(3666, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 80),
		Item_Base(3667, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 80),
		Item_Base(3668, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 80),
		Item_Base(3669, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 80),
		Item_Base(3670, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 80),
		Item_Base(3671, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 80),
		Item_Base(3672, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 80),
		Item_Base(3673, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 80),
		Item_Base(3674, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 80),
		Item_Base(3675, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 90),
		Item_Base(3676, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 90),
		Item_Base(3677, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 90),
		Item_Base(3678, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 90),
		Item_Base(3679, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 90),
		Item_Base(3680, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 90),
		Item_Base(3681, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 90),
		Item_Base(3682, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 90),
		Item_Base(3683, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 90),
		Item_Base(3684, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 90),
		Item_Base(3685, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 90),
		Item_Base(3686, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 90),
		Item_Base(3687, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 90),
		Item_Base(3688, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 90),
		Item_Base(3689, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 90),
		Item_Base(3690, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 90),
		Item_Base(3691, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 90),
		Item_Base(3692, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 90),
		Item_Base(3693, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 90),
		Item_Base(3694, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 90),
		Item_Base(3695, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 90),
		Item_Base(3696, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 90),
		Item_Base(3697, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 90),
		Item_Base(3698, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 90),
		Item_Base(3699, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 90),
		Item_Base(3700, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 100),
		Item_Base(3701, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 100),
		Item_Base(3702, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 100),
		Item_Base(3703, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 100),
		Item_Base(3704, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 100),
		Item_Base(3705, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 100),
		Item_Base(3706, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 100),
		Item_Base(3707, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 100),
		Item_Base(3708, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 100),
		Item_Base(3709, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 100),
		Item_Base(3710, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 100),
		Item_Base(3711, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 100),
		Item_Base(3712, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 100),
		Item_Base(3713, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 100),
		Item_Base(3714, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 100),
		Item_Base(3715, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 100),
		Item_Base(3716, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 100),
		Item_Base(3717, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 100),
		Item_Base(3718, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 100),
		Item_Base(3719, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 100),
		Item_Base(3720, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 100),
		Item_Base(3721, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 100),
		Item_Base(3722, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 100),
		Item_Base(3723, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 100),
		Item_Base(3724, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 100),
		Item_Base(3725, LoC::Unique, LoC::Support, LoC::SlotEar, 546, 110),
		Item_Base(3726, LoC::Unique, LoC::Support, LoC::SlotNeck, 546, 110),
		Item_Base(3727, LoC::Unique, LoC::Support, LoC::SlotFace, 546, 110),
		Item_Base(3728, LoC::Unique, LoC::Support, LoC::SlotHead, 546, 110),
		Item_Base(3729, LoC::Unique, LoC::Support, LoC::SlotFingers, 546, 110),
		Item_Base(3730, LoC::Unique, LoC::Support, LoC::SlotWrist, 546, 110),
		Item_Base(3731, LoC::Unique, LoC::Support, LoC::SlotArms, 546, 110),
		Item_Base(3732, LoC::Unique, LoC::Support, LoC::SlotHand, 546, 110),
		Item_Base(3733, LoC::Unique, LoC::Support, LoC::SlotShoulders, 546, 110),
		Item_Base(3734, LoC::Unique, LoC::Support, LoC::SlotChest, 546, 110),
		Item_Base(3735, LoC::Unique, LoC::Support, LoC::SlotBack, 546, 110),
		Item_Base(3736, LoC::Unique, LoC::Support, LoC::SlotWaist, 546, 110),
		Item_Base(3737, LoC::Unique, LoC::Support, LoC::SlotLegs, 546, 110),
		Item_Base(3738, LoC::Unique, LoC::Support, LoC::SlotFeet, 546, 110),
		Item_Base(3739, LoC::Unique, LoC::Support, LoC::SlotCharm, 546, 110),
		Item_Base(3740, LoC::Unique, LoC::Support, LoC::SlotPowerSource, 546, 110),
		Item_Base(3741, LoC::Unique, LoC::Support, LoC::SlotOneHB, 546, 110),
		Item_Base(3742, LoC::Unique, LoC::Support, LoC::SlotTwoHB, 546, 110),
		Item_Base(3743, LoC::Unique, LoC::Support, LoC::SlotOneHS, 546, 110),
		Item_Base(3744, LoC::Unique, LoC::Support, LoC::SlotTwoHS, 546, 110),
		Item_Base(3745, LoC::Unique, LoC::Support, LoC::SlotArchery, 546, 110),
		Item_Base(3746, LoC::Unique, LoC::Support, LoC::SlotThrowing, 546, 110),
		Item_Base(3747, LoC::Unique, LoC::Support, LoC::SlotOneHP, 546, 110),
		Item_Base(3748, LoC::Unique, LoC::Support, LoC::SlotTwoHP, 546, 110),
		Item_Base(3749, LoC::Unique, LoC::Support, LoC::SlotShield, 546, 110),
		Item_Base(3750, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 10),
		Item_Base(3751, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 10),
		Item_Base(3752, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 10),
		Item_Base(3753, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 10),
		Item_Base(3754, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 10),
		Item_Base(3755, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 10),
		Item_Base(3756, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 10),
		Item_Base(3757, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 10),
		Item_Base(3758, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 10),
		Item_Base(3759, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 10),
		Item_Base(3760, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 10),
		Item_Base(3761, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 10),
		Item_Base(3762, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 10),
		Item_Base(3763, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 10),
		Item_Base(3764, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 10),
		Item_Base(3765, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 10),
		Item_Base(3766, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 10),
		Item_Base(3767, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 10),
		Item_Base(3768, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 10),
		Item_Base(3769, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 10),
		Item_Base(3770, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 10),
		Item_Base(3771, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 10),
		Item_Base(3772, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 10),
		Item_Base(3773, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 10),
		Item_Base(3774, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 10),
		Item_Base(3775, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 20),
		Item_Base(3776, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 20),
		Item_Base(3777, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 20),
		Item_Base(3778, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 20),
		Item_Base(3779, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 20),
		Item_Base(3780, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 20),
		Item_Base(3781, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 20),
		Item_Base(3782, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 20),
		Item_Base(3783, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 20),
		Item_Base(3784, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 20),
		Item_Base(3785, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 20),
		Item_Base(3786, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 20),
		Item_Base(3787, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 20),
		Item_Base(3788, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 20),
		Item_Base(3789, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 20),
		Item_Base(3790, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 20),
		Item_Base(3791, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 20),
		Item_Base(3792, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 20),
		Item_Base(3793, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 20),
		Item_Base(3794, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 20),
		Item_Base(3795, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 20),
		Item_Base(3796, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 20),
		Item_Base(3797, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 20),
		Item_Base(3798, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 20),
		Item_Base(3799, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 20),
		Item_Base(3800, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 30),
		Item_Base(3801, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 30),
		Item_Base(3802, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 30),
		Item_Base(3803, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 30),
		Item_Base(3804, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 30),
		Item_Base(3805, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 30),
		Item_Base(3806, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 30),
		Item_Base(3807, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 30),
		Item_Base(3808, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 30),
		Item_Base(3809, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 30),
		Item_Base(3810, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 30),
		Item_Base(3811, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 30),
		Item_Base(3812, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 30),
		Item_Base(3813, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 30),
		Item_Base(3814, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 30),
		Item_Base(3815, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 30),
		Item_Base(3816, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 30),
		Item_Base(3817, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 30),
		Item_Base(3818, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 30),
		Item_Base(3819, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 30),
		Item_Base(3820, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 30),
		Item_Base(3821, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 30),
		Item_Base(3822, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 30),
		Item_Base(3823, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 30),
		Item_Base(3824, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 30),
		Item_Base(3825, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 40),
		Item_Base(3826, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 40),
		Item_Base(3827, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 40),
		Item_Base(3828, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 40),
		Item_Base(3829, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 40),
		Item_Base(3830, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 40),
		Item_Base(3831, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 40),
		Item_Base(3832, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 40),
		Item_Base(3833, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 40),
		Item_Base(3834, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 40),
		Item_Base(3835, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 40),
		Item_Base(3836, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 40),
		Item_Base(3837, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 40),
		Item_Base(3838, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 40),
		Item_Base(3839, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 40),
		Item_Base(3840, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 40),
		Item_Base(3841, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 40),
		Item_Base(3842, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 40),
		Item_Base(3843, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 40),
		Item_Base(3844, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 40),
		Item_Base(3845, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 40),
		Item_Base(3846, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 40),
		Item_Base(3847, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 40),
		Item_Base(3848, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 40),
		Item_Base(3849, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 40),
		Item_Base(3850, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 50),
		Item_Base(3851, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 50),
		Item_Base(3852, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 50),
		Item_Base(3853, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 50),
		Item_Base(3854, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 50),
		Item_Base(3855, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 50),
		Item_Base(3856, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 50),
		Item_Base(3857, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 50),
		Item_Base(3858, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 50),
		Item_Base(3859, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 50),
		Item_Base(3860, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 50),
		Item_Base(3861, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 50),
		Item_Base(3862, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 50),
		Item_Base(3863, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 50),
		Item_Base(3864, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 50),
		Item_Base(3865, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 50),
		Item_Base(3866, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 50),
		Item_Base(3867, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 50),
		Item_Base(3868, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 50),
		Item_Base(3869, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 50),
		Item_Base(3870, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 50),
		Item_Base(3871, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 50),
		Item_Base(3872, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 50),
		Item_Base(3873, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 50),
		Item_Base(3874, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 50),
		Item_Base(3875, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 60),
		Item_Base(3876, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 60),
		Item_Base(3877, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 60),
		Item_Base(3878, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 60),
		Item_Base(3879, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 60),
		Item_Base(3880, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 60),
		Item_Base(3881, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 60),
		Item_Base(3882, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 60),
		Item_Base(3883, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 60),
		Item_Base(3884, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 60),
		Item_Base(3885, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 60),
		Item_Base(3886, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 60),
		Item_Base(3887, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 60),
		Item_Base(3888, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 60),
		Item_Base(3889, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 60),
		Item_Base(3890, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 60),
		Item_Base(3891, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 60),
		Item_Base(3892, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 60),
		Item_Base(3893, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 60),
		Item_Base(3894, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 60),
		Item_Base(3895, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 60),
		Item_Base(3896, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 60),
		Item_Base(3897, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 60),
		Item_Base(3898, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 60),
		Item_Base(3899, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 60),
		Item_Base(3900, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 70),
		Item_Base(3901, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 70),
		Item_Base(3902, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 70),
		Item_Base(3903, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 70),
		Item_Base(3904, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 70),
		Item_Base(3905, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 70),
		Item_Base(3906, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 70),
		Item_Base(3907, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 70),
		Item_Base(3908, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 70),
		Item_Base(3909, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 70),
		Item_Base(3910, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 70),
		Item_Base(3911, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 70),
		Item_Base(3912, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 70),
		Item_Base(3913, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 70),
		Item_Base(3914, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 70),
		Item_Base(3915, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 70),
		Item_Base(3916, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 70),
		Item_Base(3917, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 70),
		Item_Base(3918, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 70),
		Item_Base(3919, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 70),
		Item_Base(3920, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 70),
		Item_Base(3921, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 70),
		Item_Base(3922, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 70),
		Item_Base(3923, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 70),
		Item_Base(3924, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 70),
		Item_Base(3925, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 80),
		Item_Base(3926, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 80),
		Item_Base(3927, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 80),
		Item_Base(3928, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 80),
		Item_Base(3929, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 80),
		Item_Base(3930, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 80),
		Item_Base(3931, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 80),
		Item_Base(3932, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 80),
		Item_Base(3933, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 80),
		Item_Base(3934, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 80),
		Item_Base(3935, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 80),
		Item_Base(3936, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 80),
		Item_Base(3937, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 80),
		Item_Base(3938, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 80),
		Item_Base(3939, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 80),
		Item_Base(3940, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 80),
		Item_Base(3941, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 80),
		Item_Base(3942, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 80),
		Item_Base(3943, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 80),
		Item_Base(3944, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 80),
		Item_Base(3945, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 80),
		Item_Base(3946, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 80),
		Item_Base(3947, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 80),
		Item_Base(3948, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 80),
		Item_Base(3949, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 80),
		Item_Base(3950, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 90),
		Item_Base(3951, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 90),
		Item_Base(3952, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 90),
		Item_Base(3953, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 90),
		Item_Base(3954, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 90),
		Item_Base(3955, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 90),
		Item_Base(3956, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 90),
		Item_Base(3957, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 90),
		Item_Base(3958, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 90),
		Item_Base(3959, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 90),
		Item_Base(3960, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 90),
		Item_Base(3961, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 90),
		Item_Base(3962, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 90),
		Item_Base(3963, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 90),
		Item_Base(3964, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 90),
		Item_Base(3965, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 90),
		Item_Base(3966, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 90),
		Item_Base(3967, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 90),
		Item_Base(3968, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 90),
		Item_Base(3969, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 90),
		Item_Base(3970, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 90),
		Item_Base(3971, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 90),
		Item_Base(3972, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 90),
		Item_Base(3973, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 90),
		Item_Base(3974, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 90),
		Item_Base(3975, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 100),
		Item_Base(3976, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 100),
		Item_Base(3977, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 100),
		Item_Base(3978, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 100),
		Item_Base(3979, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 100),
		Item_Base(3980, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 100),
		Item_Base(3981, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 100),
		Item_Base(3982, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 100),
		Item_Base(3983, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 100),
		Item_Base(3984, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 100),
		Item_Base(3985, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 100),
		Item_Base(3986, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 100),
		Item_Base(3987, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 100),
		Item_Base(3988, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 100),
		Item_Base(3989, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 100),
		Item_Base(3990, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 100),
		Item_Base(3991, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 100),
		Item_Base(3992, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 100),
		Item_Base(3993, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 100),
		Item_Base(3994, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 100),
		Item_Base(3995, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 100),
		Item_Base(3996, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 100),
		Item_Base(3997, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 100),
		Item_Base(3998, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 100),
		Item_Base(3999, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 100),
		Item_Base(4000, LoC::Common, LoC::Damage, LoC::SlotEar, 49608, 110),
		Item_Base(4001, LoC::Common, LoC::Damage, LoC::SlotNeck, 49608, 110),
		Item_Base(4002, LoC::Common, LoC::Damage, LoC::SlotFace, 49608, 110),
		Item_Base(4003, LoC::Common, LoC::Damage, LoC::SlotHead, 49608, 110),
		Item_Base(4004, LoC::Common, LoC::Damage, LoC::SlotFingers, 49608, 110),
		Item_Base(4005, LoC::Common, LoC::Damage, LoC::SlotWrist, 49608, 110),
		Item_Base(4006, LoC::Common, LoC::Damage, LoC::SlotArms, 49608, 110),
		Item_Base(4007, LoC::Common, LoC::Damage, LoC::SlotHand, 49608, 110),
		Item_Base(4008, LoC::Common, LoC::Damage, LoC::SlotShoulders, 49608, 110),
		Item_Base(4009, LoC::Common, LoC::Damage, LoC::SlotChest, 49608, 110),
		Item_Base(4010, LoC::Common, LoC::Damage, LoC::SlotBack, 49608, 110),
		Item_Base(4011, LoC::Common, LoC::Damage, LoC::SlotWaist, 49608, 110),
		Item_Base(4012, LoC::Common, LoC::Damage, LoC::SlotLegs, 49608, 110),
		Item_Base(4013, LoC::Common, LoC::Damage, LoC::SlotFeet, 49608, 110),
		Item_Base(4014, LoC::Common, LoC::Damage, LoC::SlotCharm, 49608, 110),
		Item_Base(4015, LoC::Common, LoC::Damage, LoC::SlotPowerSource, 49608, 110),
		Item_Base(4016, LoC::Common, LoC::Damage, LoC::SlotOneHB, 49608, 110),
		Item_Base(4017, LoC::Common, LoC::Damage, LoC::SlotTwoHB, 49608, 110),
		Item_Base(4018, LoC::Common, LoC::Damage, LoC::SlotOneHS, 49608, 110),
		Item_Base(4019, LoC::Common, LoC::Damage, LoC::SlotTwoHS, 49608, 110),
		Item_Base(4020, LoC::Common, LoC::Damage, LoC::SlotArchery, 49608, 110),
		Item_Base(4021, LoC::Common, LoC::Damage, LoC::SlotThrowing, 49608, 110),
		Item_Base(4022, LoC::Common, LoC::Damage, LoC::SlotOneHP, 49608, 110),
		Item_Base(4023, LoC::Common, LoC::Damage, LoC::SlotTwoHP, 49608, 110),
		Item_Base(4024, LoC::Common, LoC::Damage, LoC::SlotShield, 49608, 110),
		Item_Base(4025, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 10),
		Item_Base(4026, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 10),
		Item_Base(4027, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 10),
		Item_Base(4028, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 10),
		Item_Base(4029, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 10),
		Item_Base(4030, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 10),
		Item_Base(4031, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 10),
		Item_Base(4032, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 10),
		Item_Base(4033, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 10),
		Item_Base(4034, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 10),
		Item_Base(4035, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 10),
		Item_Base(4036, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 10),
		Item_Base(4037, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 10),
		Item_Base(4038, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 10),
		Item_Base(4039, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 10),
		Item_Base(4040, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 10),
		Item_Base(4041, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 10),
		Item_Base(4042, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 10),
		Item_Base(4043, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 10),
		Item_Base(4044, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 10),
		Item_Base(4045, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 10),
		Item_Base(4046, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 10),
		Item_Base(4047, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 10),
		Item_Base(4048, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 10),
		Item_Base(4049, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 10),
		Item_Base(4050, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 20),
		Item_Base(4051, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 20),
		Item_Base(4052, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 20),
		Item_Base(4053, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 20),
		Item_Base(4054, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 20),
		Item_Base(4055, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 20),
		Item_Base(4056, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 20),
		Item_Base(4057, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 20),
		Item_Base(4058, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 20),
		Item_Base(4059, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 20),
		Item_Base(4060, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 20),
		Item_Base(4061, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 20),
		Item_Base(4062, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 20),
		Item_Base(4063, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 20),
		Item_Base(4064, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 20),
		Item_Base(4065, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 20),
		Item_Base(4066, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 20),
		Item_Base(4067, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 20),
		Item_Base(4068, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 20),
		Item_Base(4069, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 20),
		Item_Base(4070, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 20),
		Item_Base(4071, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 20),
		Item_Base(4072, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 20),
		Item_Base(4073, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 20),
		Item_Base(4074, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 20),
		Item_Base(4075, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 30),
		Item_Base(4076, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 30),
		Item_Base(4077, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 30),
		Item_Base(4078, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 30),
		Item_Base(4079, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 30),
		Item_Base(4080, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 30),
		Item_Base(4081, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 30),
		Item_Base(4082, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 30),
		Item_Base(4083, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 30),
		Item_Base(4084, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 30),
		Item_Base(4085, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 30),
		Item_Base(4086, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 30),
		Item_Base(4087, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 30),
		Item_Base(4088, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 30),
		Item_Base(4089, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 30),
		Item_Base(4090, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 30),
		Item_Base(4091, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 30),
		Item_Base(4092, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 30),
		Item_Base(4093, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 30),
		Item_Base(4094, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 30),
		Item_Base(4095, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 30),
		Item_Base(4096, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 30),
		Item_Base(4097, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 30),
		Item_Base(4098, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 30),
		Item_Base(4099, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 30),
		Item_Base(4100, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 40),
		Item_Base(4101, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 40),
		Item_Base(4102, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 40),
		Item_Base(4103, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 40),
		Item_Base(4104, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 40),
		Item_Base(4105, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 40),
		Item_Base(4106, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 40),
		Item_Base(4107, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 40),
		Item_Base(4108, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 40),
		Item_Base(4109, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 40),
		Item_Base(4110, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 40),
		Item_Base(4111, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 40),
		Item_Base(4112, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 40),
		Item_Base(4113, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 40),
		Item_Base(4114, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 40),
		Item_Base(4115, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 40),
		Item_Base(4116, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 40),
		Item_Base(4117, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 40),
		Item_Base(4118, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 40),
		Item_Base(4119, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 40),
		Item_Base(4120, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 40),
		Item_Base(4121, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 40),
		Item_Base(4122, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 40),
		Item_Base(4123, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 40),
		Item_Base(4124, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 40),
		Item_Base(4125, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 50),
		Item_Base(4126, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 50),
		Item_Base(4127, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 50),
		Item_Base(4128, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 50),
		Item_Base(4129, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 50),
		Item_Base(4130, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 50),
		Item_Base(4131, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 50),
		Item_Base(4132, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 50),
		Item_Base(4133, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 50),
		Item_Base(4134, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 50),
		Item_Base(4135, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 50),
		Item_Base(4136, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 50),
		Item_Base(4137, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 50),
		Item_Base(4138, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 50),
		Item_Base(4139, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 50),
		Item_Base(4140, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 50),
		Item_Base(4141, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 50),
		Item_Base(4142, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 50),
		Item_Base(4143, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 50),
		Item_Base(4144, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 50),
		Item_Base(4145, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 50),
		Item_Base(4146, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 50),
		Item_Base(4147, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 50),
		Item_Base(4148, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 50),
		Item_Base(4149, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 50),
		Item_Base(4150, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 60),
		Item_Base(4151, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 60),
		Item_Base(4152, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 60),
		Item_Base(4153, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 60),
		Item_Base(4154, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 60),
		Item_Base(4155, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 60),
		Item_Base(4156, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 60),
		Item_Base(4157, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 60),
		Item_Base(4158, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 60),
		Item_Base(4159, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 60),
		Item_Base(4160, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 60),
		Item_Base(4161, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 60),
		Item_Base(4162, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 60),
		Item_Base(4163, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 60),
		Item_Base(4164, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 60),
		Item_Base(4165, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 60),
		Item_Base(4166, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 60),
		Item_Base(4167, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 60),
		Item_Base(4168, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 60),
		Item_Base(4169, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 60),
		Item_Base(4170, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 60),
		Item_Base(4171, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 60),
		Item_Base(4172, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 60),
		Item_Base(4173, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 60),
		Item_Base(4174, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 60),
		Item_Base(4175, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 70),
		Item_Base(4176, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 70),
		Item_Base(4177, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 70),
		Item_Base(4178, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 70),
		Item_Base(4179, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 70),
		Item_Base(4180, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 70),
		Item_Base(4181, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 70),
		Item_Base(4182, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 70),
		Item_Base(4183, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 70),
		Item_Base(4184, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 70),
		Item_Base(4185, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 70),
		Item_Base(4186, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 70),
		Item_Base(4187, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 70),
		Item_Base(4188, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 70),
		Item_Base(4189, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 70),
		Item_Base(4190, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 70),
		Item_Base(4191, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 70),
		Item_Base(4192, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 70),
		Item_Base(4193, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 70),
		Item_Base(4194, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 70),
		Item_Base(4195, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 70),
		Item_Base(4196, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 70),
		Item_Base(4197, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 70),
		Item_Base(4198, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 70),
		Item_Base(4199, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 70),
		Item_Base(4200, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 80),
		Item_Base(4201, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 80),
		Item_Base(4202, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 80),
		Item_Base(4203, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 80),
		Item_Base(4204, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 80),
		Item_Base(4205, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 80),
		Item_Base(4206, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 80),
		Item_Base(4207, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 80),
		Item_Base(4208, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 80),
		Item_Base(4209, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 80),
		Item_Base(4210, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 80),
		Item_Base(4211, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 80),
		Item_Base(4212, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 80),
		Item_Base(4213, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 80),
		Item_Base(4214, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 80),
		Item_Base(4215, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 80),
		Item_Base(4216, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 80),
		Item_Base(4217, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 80),
		Item_Base(4218, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 80),
		Item_Base(4219, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 80),
		Item_Base(4220, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 80),
		Item_Base(4221, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 80),
		Item_Base(4222, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 80),
		Item_Base(4223, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 80),
		Item_Base(4224, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 80),
		Item_Base(4225, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 90),
		Item_Base(4226, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 90),
		Item_Base(4227, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 90),
		Item_Base(4228, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 90),
		Item_Base(4229, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 90),
		Item_Base(4230, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 90),
		Item_Base(4231, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 90),
		Item_Base(4232, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 90),
		Item_Base(4233, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 90),
		Item_Base(4234, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 90),
		Item_Base(4235, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 90),
		Item_Base(4236, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 90),
		Item_Base(4237, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 90),
		Item_Base(4238, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 90),
		Item_Base(4239, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 90),
		Item_Base(4240, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 90),
		Item_Base(4241, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 90),
		Item_Base(4242, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 90),
		Item_Base(4243, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 90),
		Item_Base(4244, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 90),
		Item_Base(4245, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 90),
		Item_Base(4246, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 90),
		Item_Base(4247, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 90),
		Item_Base(4248, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 90),
		Item_Base(4249, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 90),
		Item_Base(4250, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 100),
		Item_Base(4251, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 100),
		Item_Base(4252, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 100),
		Item_Base(4253, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 100),
		Item_Base(4254, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 100),
		Item_Base(4255, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 100),
		Item_Base(4256, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 100),
		Item_Base(4257, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 100),
		Item_Base(4258, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 100),
		Item_Base(4259, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 100),
		Item_Base(4260, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 100),
		Item_Base(4261, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 100),
		Item_Base(4262, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 100),
		Item_Base(4263, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 100),
		Item_Base(4264, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 100),
		Item_Base(4265, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 100),
		Item_Base(4266, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 100),
		Item_Base(4267, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 100),
		Item_Base(4268, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 100),
		Item_Base(4269, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 100),
		Item_Base(4270, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 100),
		Item_Base(4271, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 100),
		Item_Base(4272, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 100),
		Item_Base(4273, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 100),
		Item_Base(4274, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 100),
		Item_Base(4275, LoC::Uncommon, LoC::Damage, LoC::SlotEar, 49608, 110),
		Item_Base(4276, LoC::Uncommon, LoC::Damage, LoC::SlotNeck, 49608, 110),
		Item_Base(4277, LoC::Uncommon, LoC::Damage, LoC::SlotFace, 49608, 110),
		Item_Base(4278, LoC::Uncommon, LoC::Damage, LoC::SlotHead, 49608, 110),
		Item_Base(4279, LoC::Uncommon, LoC::Damage, LoC::SlotFingers, 49608, 110),
		Item_Base(4280, LoC::Uncommon, LoC::Damage, LoC::SlotWrist, 49608, 110),
		Item_Base(4281, LoC::Uncommon, LoC::Damage, LoC::SlotArms, 49608, 110),
		Item_Base(4282, LoC::Uncommon, LoC::Damage, LoC::SlotHand, 49608, 110),
		Item_Base(4283, LoC::Uncommon, LoC::Damage, LoC::SlotShoulders, 49608, 110),
		Item_Base(4284, LoC::Uncommon, LoC::Damage, LoC::SlotChest, 49608, 110),
		Item_Base(4285, LoC::Uncommon, LoC::Damage, LoC::SlotBack, 49608, 110),
		Item_Base(4286, LoC::Uncommon, LoC::Damage, LoC::SlotWaist, 49608, 110),
		Item_Base(4287, LoC::Uncommon, LoC::Damage, LoC::SlotLegs, 49608, 110),
		Item_Base(4288, LoC::Uncommon, LoC::Damage, LoC::SlotFeet, 49608, 110),
		Item_Base(4289, LoC::Uncommon, LoC::Damage, LoC::SlotCharm, 49608, 110),
		Item_Base(4290, LoC::Uncommon, LoC::Damage, LoC::SlotPowerSource, 49608, 110),
		Item_Base(4291, LoC::Uncommon, LoC::Damage, LoC::SlotOneHB, 49608, 110),
		Item_Base(4292, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHB, 49608, 110),
		Item_Base(4293, LoC::Uncommon, LoC::Damage, LoC::SlotOneHS, 49608, 110),
		Item_Base(4294, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHS, 49608, 110),
		Item_Base(4295, LoC::Uncommon, LoC::Damage, LoC::SlotArchery, 49608, 110),
		Item_Base(4296, LoC::Uncommon, LoC::Damage, LoC::SlotThrowing, 49608, 110),
		Item_Base(4297, LoC::Uncommon, LoC::Damage, LoC::SlotOneHP, 49608, 110),
		Item_Base(4298, LoC::Uncommon, LoC::Damage, LoC::SlotTwoHP, 49608, 110),
		Item_Base(4299, LoC::Uncommon, LoC::Damage, LoC::SlotShield, 49608, 110),
		Item_Base(4300, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 10),
		Item_Base(4301, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 10),
		Item_Base(4302, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 10),
		Item_Base(4303, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 10),
		Item_Base(4304, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 10),
		Item_Base(4305, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 10),
		Item_Base(4306, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 10),
		Item_Base(4307, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 10),
		Item_Base(4308, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 10),
		Item_Base(4309, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 10),
		Item_Base(4310, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 10),
		Item_Base(4311, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 10),
		Item_Base(4312, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 10),
		Item_Base(4313, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 10),
		Item_Base(4314, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 10),
		Item_Base(4315, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 10),
		Item_Base(4316, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 10),
		Item_Base(4317, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 10),
		Item_Base(4318, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 10),
		Item_Base(4319, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 10),
		Item_Base(4320, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 10),
		Item_Base(4321, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 10),
		Item_Base(4322, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 10),
		Item_Base(4323, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 10),
		Item_Base(4324, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 10),
		Item_Base(4325, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 20),
		Item_Base(4326, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 20),
		Item_Base(4327, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 20),
		Item_Base(4328, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 20),
		Item_Base(4329, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 20),
		Item_Base(4330, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 20),
		Item_Base(4331, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 20),
		Item_Base(4332, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 20),
		Item_Base(4333, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 20),
		Item_Base(4334, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 20),
		Item_Base(4335, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 20),
		Item_Base(4336, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 20),
		Item_Base(4337, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 20),
		Item_Base(4338, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 20),
		Item_Base(4339, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 20),
		Item_Base(4340, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 20),
		Item_Base(4341, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 20),
		Item_Base(4342, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 20),
		Item_Base(4343, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 20),
		Item_Base(4344, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 20),
		Item_Base(4345, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 20),
		Item_Base(4346, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 20),
		Item_Base(4347, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 20),
		Item_Base(4348, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 20),
		Item_Base(4349, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 20),
		Item_Base(4350, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 30),
		Item_Base(4351, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 30),
		Item_Base(4352, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 30),
		Item_Base(4353, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 30),
		Item_Base(4354, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 30),
		Item_Base(4355, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 30),
		Item_Base(4356, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 30),
		Item_Base(4357, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 30),
		Item_Base(4358, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 30),
		Item_Base(4359, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 30),
		Item_Base(4360, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 30),
		Item_Base(4361, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 30),
		Item_Base(4362, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 30),
		Item_Base(4363, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 30),
		Item_Base(4364, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 30),
		Item_Base(4365, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 30),
		Item_Base(4366, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 30),
		Item_Base(4367, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 30),
		Item_Base(4368, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 30),
		Item_Base(4369, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 30),
		Item_Base(4370, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 30),
		Item_Base(4371, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 30),
		Item_Base(4372, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 30),
		Item_Base(4373, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 30),
		Item_Base(4374, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 30),
		Item_Base(4375, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 40),
		Item_Base(4376, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 40),
		Item_Base(4377, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 40),
		Item_Base(4378, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 40),
		Item_Base(4379, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 40),
		Item_Base(4380, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 40),
		Item_Base(4381, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 40),
		Item_Base(4382, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 40),
		Item_Base(4383, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 40),
		Item_Base(4384, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 40),
		Item_Base(4385, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 40),
		Item_Base(4386, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 40),
		Item_Base(4387, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 40),
		Item_Base(4388, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 40),
		Item_Base(4389, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 40),
		Item_Base(4390, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 40),
		Item_Base(4391, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 40),
		Item_Base(4392, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 40),
		Item_Base(4393, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 40),
		Item_Base(4394, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 40),
		Item_Base(4395, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 40),
		Item_Base(4396, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 40),
		Item_Base(4397, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 40),
		Item_Base(4398, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 40),
		Item_Base(4399, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 40),
		Item_Base(4400, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 50),
		Item_Base(4401, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 50),
		Item_Base(4402, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 50),
		Item_Base(4403, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 50),
		Item_Base(4404, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 50),
		Item_Base(4405, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 50),
		Item_Base(4406, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 50),
		Item_Base(4407, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 50),
		Item_Base(4408, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 50),
		Item_Base(4409, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 50),
		Item_Base(4410, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 50),
		Item_Base(4411, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 50),
		Item_Base(4412, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 50),
		Item_Base(4413, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 50),
		Item_Base(4414, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 50),
		Item_Base(4415, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 50),
		Item_Base(4416, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 50),
		Item_Base(4417, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 50),
		Item_Base(4418, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 50),
		Item_Base(4419, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 50),
		Item_Base(4420, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 50),
		Item_Base(4421, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 50),
		Item_Base(4422, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 50),
		Item_Base(4423, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 50),
		Item_Base(4424, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 50),
		Item_Base(4425, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 60),
		Item_Base(4426, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 60),
		Item_Base(4427, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 60),
		Item_Base(4428, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 60),
		Item_Base(4429, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 60),
		Item_Base(4430, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 60),
		Item_Base(4431, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 60),
		Item_Base(4432, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 60),
		Item_Base(4433, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 60),
		Item_Base(4434, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 60),
		Item_Base(4435, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 60),
		Item_Base(4436, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 60),
		Item_Base(4437, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 60),
		Item_Base(4438, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 60),
		Item_Base(4439, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 60),
		Item_Base(4440, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 60),
		Item_Base(4441, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 60),
		Item_Base(4442, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 60),
		Item_Base(4443, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 60),
		Item_Base(4444, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 60),
		Item_Base(4445, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 60),
		Item_Base(4446, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 60),
		Item_Base(4447, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 60),
		Item_Base(4448, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 60),
		Item_Base(4449, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 60),
		Item_Base(4450, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 70),
		Item_Base(4451, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 70),
		Item_Base(4452, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 70),
		Item_Base(4453, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 70),
		Item_Base(4454, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 70),
		Item_Base(4455, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 70),
		Item_Base(4456, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 70),
		Item_Base(4457, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 70),
		Item_Base(4458, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 70),
		Item_Base(4459, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 70),
		Item_Base(4460, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 70),
		Item_Base(4461, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 70),
		Item_Base(4462, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 70),
		Item_Base(4463, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 70),
		Item_Base(4464, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 70),
		Item_Base(4465, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 70),
		Item_Base(4466, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 70),
		Item_Base(4467, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 70),
		Item_Base(4468, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 70),
		Item_Base(4469, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 70),
		Item_Base(4470, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 70),
		Item_Base(4471, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 70),
		Item_Base(4472, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 70),
		Item_Base(4473, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 70),
		Item_Base(4474, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 70),
		Item_Base(4475, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 80),
		Item_Base(4476, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 80),
		Item_Base(4477, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 80),
		Item_Base(4478, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 80),
		Item_Base(4479, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 80),
		Item_Base(4480, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 80),
		Item_Base(4481, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 80),
		Item_Base(4482, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 80),
		Item_Base(4483, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 80),
		Item_Base(4484, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 80),
		Item_Base(4485, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 80),
		Item_Base(4486, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 80),
		Item_Base(4487, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 80),
		Item_Base(4488, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 80),
		Item_Base(4489, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 80),
		Item_Base(4490, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 80),
		Item_Base(4491, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 80),
		Item_Base(4492, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 80),
		Item_Base(4493, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 80),
		Item_Base(4494, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 80),
		Item_Base(4495, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 80),
		Item_Base(4496, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 80),
		Item_Base(4497, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 80),
		Item_Base(4498, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 80),
		Item_Base(4499, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 80),
		Item_Base(4500, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 90),
		Item_Base(4501, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 90),
		Item_Base(4502, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 90),
		Item_Base(4503, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 90),
		Item_Base(4504, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 90),
		Item_Base(4505, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 90),
		Item_Base(4506, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 90),
		Item_Base(4507, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 90),
		Item_Base(4508, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 90),
		Item_Base(4509, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 90),
		Item_Base(4510, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 90),
		Item_Base(4511, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 90),
		Item_Base(4512, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 90),
		Item_Base(4513, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 90),
		Item_Base(4514, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 90),
		Item_Base(4515, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 90),
		Item_Base(4516, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 90),
		Item_Base(4517, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 90),
		Item_Base(4518, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 90),
		Item_Base(4519, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 90),
		Item_Base(4520, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 90),
		Item_Base(4521, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 90),
		Item_Base(4522, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 90),
		Item_Base(4523, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 90),
		Item_Base(4524, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 90),
		Item_Base(4525, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 100),
		Item_Base(4526, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 100),
		Item_Base(4527, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 100),
		Item_Base(4528, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 100),
		Item_Base(4529, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 100),
		Item_Base(4530, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 100),
		Item_Base(4531, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 100),
		Item_Base(4532, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 100),
		Item_Base(4533, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 100),
		Item_Base(4534, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 100),
		Item_Base(4535, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 100),
		Item_Base(4536, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 100),
		Item_Base(4537, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 100),
		Item_Base(4538, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 100),
		Item_Base(4539, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 100),
		Item_Base(4540, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 100),
		Item_Base(4541, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 100),
		Item_Base(4542, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 100),
		Item_Base(4543, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 100),
		Item_Base(4544, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 100),
		Item_Base(4545, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 100),
		Item_Base(4546, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 100),
		Item_Base(4547, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 100),
		Item_Base(4548, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 100),
		Item_Base(4549, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 100),
		Item_Base(4550, LoC::Rare, LoC::Damage, LoC::SlotEar, 49608, 110),
		Item_Base(4551, LoC::Rare, LoC::Damage, LoC::SlotNeck, 49608, 110),
		Item_Base(4552, LoC::Rare, LoC::Damage, LoC::SlotFace, 49608, 110),
		Item_Base(4553, LoC::Rare, LoC::Damage, LoC::SlotHead, 49608, 110),
		Item_Base(4554, LoC::Rare, LoC::Damage, LoC::SlotFingers, 49608, 110),
		Item_Base(4555, LoC::Rare, LoC::Damage, LoC::SlotWrist, 49608, 110),
		Item_Base(4556, LoC::Rare, LoC::Damage, LoC::SlotArms, 49608, 110),
		Item_Base(4557, LoC::Rare, LoC::Damage, LoC::SlotHand, 49608, 110),
		Item_Base(4558, LoC::Rare, LoC::Damage, LoC::SlotShoulders, 49608, 110),
		Item_Base(4559, LoC::Rare, LoC::Damage, LoC::SlotChest, 49608, 110),
		Item_Base(4560, LoC::Rare, LoC::Damage, LoC::SlotBack, 49608, 110),
		Item_Base(4561, LoC::Rare, LoC::Damage, LoC::SlotWaist, 49608, 110),
		Item_Base(4562, LoC::Rare, LoC::Damage, LoC::SlotLegs, 49608, 110),
		Item_Base(4563, LoC::Rare, LoC::Damage, LoC::SlotFeet, 49608, 110),
		Item_Base(4564, LoC::Rare, LoC::Damage, LoC::SlotCharm, 49608, 110),
		Item_Base(4565, LoC::Rare, LoC::Damage, LoC::SlotPowerSource, 49608, 110),
		Item_Base(4566, LoC::Rare, LoC::Damage, LoC::SlotOneHB, 49608, 110),
		Item_Base(4567, LoC::Rare, LoC::Damage, LoC::SlotTwoHB, 49608, 110),
		Item_Base(4568, LoC::Rare, LoC::Damage, LoC::SlotOneHS, 49608, 110),
		Item_Base(4569, LoC::Rare, LoC::Damage, LoC::SlotTwoHS, 49608, 110),
		Item_Base(4570, LoC::Rare, LoC::Damage, LoC::SlotArchery, 49608, 110),
		Item_Base(4571, LoC::Rare, LoC::Damage, LoC::SlotThrowing, 49608, 110),
		Item_Base(4572, LoC::Rare, LoC::Damage, LoC::SlotOneHP, 49608, 110),
		Item_Base(4573, LoC::Rare, LoC::Damage, LoC::SlotTwoHP, 49608, 110),
		Item_Base(4574, LoC::Rare, LoC::Damage, LoC::SlotShield, 49608, 110),
		Item_Base(4575, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 10),
		Item_Base(4576, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 10),
		Item_Base(4577, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 10),
		Item_Base(4578, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 10),
		Item_Base(4579, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 10),
		Item_Base(4580, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 10),
		Item_Base(4581, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 10),
		Item_Base(4582, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 10),
		Item_Base(4583, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 10),
		Item_Base(4584, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 10),
		Item_Base(4585, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 10),
		Item_Base(4586, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 10),
		Item_Base(4587, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 10),
		Item_Base(4588, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 10),
		Item_Base(4589, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 10),
		Item_Base(4590, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 10),
		Item_Base(4591, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 10),
		Item_Base(4592, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 10),
		Item_Base(4593, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 10),
		Item_Base(4594, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 10),
		Item_Base(4595, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 10),
		Item_Base(4596, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 10),
		Item_Base(4597, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 10),
		Item_Base(4598, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 10),
		Item_Base(4599, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 10),
		Item_Base(4600, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 20),
		Item_Base(4601, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 20),
		Item_Base(4602, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 20),
		Item_Base(4603, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 20),
		Item_Base(4604, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 20),
		Item_Base(4605, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 20),
		Item_Base(4606, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 20),
		Item_Base(4607, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 20),
		Item_Base(4608, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 20),
		Item_Base(4609, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 20),
		Item_Base(4610, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 20),
		Item_Base(4611, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 20),
		Item_Base(4612, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 20),
		Item_Base(4613, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 20),
		Item_Base(4614, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 20),
		Item_Base(4615, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 20),
		Item_Base(4616, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 20),
		Item_Base(4617, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 20),
		Item_Base(4618, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 20),
		Item_Base(4619, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 20),
		Item_Base(4620, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 20),
		Item_Base(4621, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 20),
		Item_Base(4622, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 20),
		Item_Base(4623, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 20),
		Item_Base(4624, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 20),
		Item_Base(4625, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 30),
		Item_Base(4626, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 30),
		Item_Base(4627, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 30),
		Item_Base(4628, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 30),
		Item_Base(4629, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 30),
		Item_Base(4630, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 30),
		Item_Base(4631, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 30),
		Item_Base(4632, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 30),
		Item_Base(4633, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 30),
		Item_Base(4634, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 30),
		Item_Base(4635, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 30),
		Item_Base(4636, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 30),
		Item_Base(4637, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 30),
		Item_Base(4638, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 30),
		Item_Base(4639, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 30),
		Item_Base(4640, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 30),
		Item_Base(4641, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 30),
		Item_Base(4642, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 30),
		Item_Base(4643, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 30),
		Item_Base(4644, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 30),
		Item_Base(4645, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 30),
		Item_Base(4646, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 30),
		Item_Base(4647, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 30),
		Item_Base(4648, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 30),
		Item_Base(4649, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 30),
		Item_Base(4650, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 40),
		Item_Base(4651, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 40),
		Item_Base(4652, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 40),
		Item_Base(4653, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 40),
		Item_Base(4654, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 40),
		Item_Base(4655, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 40),
		Item_Base(4656, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 40),
		Item_Base(4657, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 40),
		Item_Base(4658, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 40),
		Item_Base(4659, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 40),
		Item_Base(4660, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 40),
		Item_Base(4661, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 40),
		Item_Base(4662, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 40),
		Item_Base(4663, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 40),
		Item_Base(4664, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 40),
		Item_Base(4665, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 40),
		Item_Base(4666, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 40),
		Item_Base(4667, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 40),
		Item_Base(4668, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 40),
		Item_Base(4669, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 40),
		Item_Base(4670, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 40),
		Item_Base(4671, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 40),
		Item_Base(4672, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 40),
		Item_Base(4673, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 40),
		Item_Base(4674, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 40),
		Item_Base(4675, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 50),
		Item_Base(4676, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 50),
		Item_Base(4677, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 50),
		Item_Base(4678, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 50),
		Item_Base(4679, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 50),
		Item_Base(4680, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 50),
		Item_Base(4681, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 50),
		Item_Base(4682, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 50),
		Item_Base(4683, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 50),
		Item_Base(4684, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 50),
		Item_Base(4685, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 50),
		Item_Base(4686, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 50),
		Item_Base(4687, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 50),
		Item_Base(4688, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 50),
		Item_Base(4689, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 50),
		Item_Base(4690, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 50),
		Item_Base(4691, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 50),
		Item_Base(4692, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 50),
		Item_Base(4693, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 50),
		Item_Base(4694, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 50),
		Item_Base(4695, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 50),
		Item_Base(4696, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 50),
		Item_Base(4697, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 50),
		Item_Base(4698, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 50),
		Item_Base(4699, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 50),
		Item_Base(4700, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 60),
		Item_Base(4701, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 60),
		Item_Base(4702, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 60),
		Item_Base(4703, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 60),
		Item_Base(4704, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 60),
		Item_Base(4705, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 60),
		Item_Base(4706, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 60),
		Item_Base(4707, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 60),
		Item_Base(4708, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 60),
		Item_Base(4709, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 60),
		Item_Base(4710, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 60),
		Item_Base(4711, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 60),
		Item_Base(4712, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 60),
		Item_Base(4713, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 60),
		Item_Base(4714, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 60),
		Item_Base(4715, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 60),
		Item_Base(4716, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 60),
		Item_Base(4717, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 60),
		Item_Base(4718, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 60),
		Item_Base(4719, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 60),
		Item_Base(4720, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 60),
		Item_Base(4721, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 60),
		Item_Base(4722, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 60),
		Item_Base(4723, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 60),
		Item_Base(4724, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 60),
		Item_Base(4725, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 70),
		Item_Base(4726, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 70),
		Item_Base(4727, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 70),
		Item_Base(4728, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 70),
		Item_Base(4729, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 70),
		Item_Base(4730, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 70),
		Item_Base(4731, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 70),
		Item_Base(4732, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 70),
		Item_Base(4733, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 70),
		Item_Base(4734, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 70),
		Item_Base(4735, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 70),
		Item_Base(4736, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 70),
		Item_Base(4737, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 70),
		Item_Base(4738, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 70),
		Item_Base(4739, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 70),
		Item_Base(4740, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 70),
		Item_Base(4741, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 70),
		Item_Base(4742, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 70),
		Item_Base(4743, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 70),
		Item_Base(4744, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 70),
		Item_Base(4745, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 70),
		Item_Base(4746, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 70),
		Item_Base(4747, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 70),
		Item_Base(4748, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 70),
		Item_Base(4749, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 70),
		Item_Base(4750, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 80),
		Item_Base(4751, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 80),
		Item_Base(4752, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 80),
		Item_Base(4753, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 80),
		Item_Base(4754, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 80),
		Item_Base(4755, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 80),
		Item_Base(4756, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 80),
		Item_Base(4757, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 80),
		Item_Base(4758, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 80),
		Item_Base(4759, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 80),
		Item_Base(4760, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 80),
		Item_Base(4761, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 80),
		Item_Base(4762, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 80),
		Item_Base(4763, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 80),
		Item_Base(4764, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 80),
		Item_Base(4765, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 80),
		Item_Base(4766, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 80),
		Item_Base(4767, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 80),
		Item_Base(4768, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 80),
		Item_Base(4769, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 80),
		Item_Base(4770, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 80),
		Item_Base(4771, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 80),
		Item_Base(4772, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 80),
		Item_Base(4773, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 80),
		Item_Base(4774, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 80),
		Item_Base(4775, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 90),
		Item_Base(4776, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 90),
		Item_Base(4777, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 90),
		Item_Base(4778, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 90),
		Item_Base(4779, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 90),
		Item_Base(4780, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 90),
		Item_Base(4781, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 90),
		Item_Base(4782, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 90),
		Item_Base(4783, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 90),
		Item_Base(4784, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 90),
		Item_Base(4785, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 90),
		Item_Base(4786, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 90),
		Item_Base(4787, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 90),
		Item_Base(4788, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 90),
		Item_Base(4789, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 90),
		Item_Base(4790, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 90),
		Item_Base(4791, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 90),
		Item_Base(4792, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 90),
		Item_Base(4793, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 90),
		Item_Base(4794, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 90),
		Item_Base(4795, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 90),
		Item_Base(4796, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 90),
		Item_Base(4797, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 90),
		Item_Base(4798, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 90),
		Item_Base(4799, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 90),
		Item_Base(4800, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 100),
		Item_Base(4801, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 100),
		Item_Base(4802, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 100),
		Item_Base(4803, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 100),
		Item_Base(4804, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 100),
		Item_Base(4805, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 100),
		Item_Base(4806, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 100),
		Item_Base(4807, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 100),
		Item_Base(4808, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 100),
		Item_Base(4809, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 100),
		Item_Base(4810, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 100),
		Item_Base(4811, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 100),
		Item_Base(4812, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 100),
		Item_Base(4813, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 100),
		Item_Base(4814, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 100),
		Item_Base(4815, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 100),
		Item_Base(4816, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 100),
		Item_Base(4817, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 100),
		Item_Base(4818, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 100),
		Item_Base(4819, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 100),
		Item_Base(4820, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 100),
		Item_Base(4821, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 100),
		Item_Base(4822, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 100),
		Item_Base(4823, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 100),
		Item_Base(4824, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 100),
		Item_Base(4825, LoC::Legendary, LoC::Damage, LoC::SlotEar, 49608, 110),
		Item_Base(4826, LoC::Legendary, LoC::Damage, LoC::SlotNeck, 49608, 110),
		Item_Base(4827, LoC::Legendary, LoC::Damage, LoC::SlotFace, 49608, 110),
		Item_Base(4828, LoC::Legendary, LoC::Damage, LoC::SlotHead, 49608, 110),
		Item_Base(4829, LoC::Legendary, LoC::Damage, LoC::SlotFingers, 49608, 110),
		Item_Base(4830, LoC::Legendary, LoC::Damage, LoC::SlotWrist, 49608, 110),
		Item_Base(4831, LoC::Legendary, LoC::Damage, LoC::SlotArms, 49608, 110),
		Item_Base(4832, LoC::Legendary, LoC::Damage, LoC::SlotHand, 49608, 110),
		Item_Base(4833, LoC::Legendary, LoC::Damage, LoC::SlotShoulders, 49608, 110),
		Item_Base(4834, LoC::Legendary, LoC::Damage, LoC::SlotChest, 49608, 110),
		Item_Base(4835, LoC::Legendary, LoC::Damage, LoC::SlotBack, 49608, 110),
		Item_Base(4836, LoC::Legendary, LoC::Damage, LoC::SlotWaist, 49608, 110),
		Item_Base(4837, LoC::Legendary, LoC::Damage, LoC::SlotLegs, 49608, 110),
		Item_Base(4838, LoC::Legendary, LoC::Damage, LoC::SlotFeet, 49608, 110),
		Item_Base(4839, LoC::Legendary, LoC::Damage, LoC::SlotCharm, 49608, 110),
		Item_Base(4840, LoC::Legendary, LoC::Damage, LoC::SlotPowerSource, 49608, 110),
		Item_Base(4841, LoC::Legendary, LoC::Damage, LoC::SlotOneHB, 49608, 110),
		Item_Base(4842, LoC::Legendary, LoC::Damage, LoC::SlotTwoHB, 49608, 110),
		Item_Base(4843, LoC::Legendary, LoC::Damage, LoC::SlotOneHS, 49608, 110),
		Item_Base(4844, LoC::Legendary, LoC::Damage, LoC::SlotTwoHS, 49608, 110),
		Item_Base(4845, LoC::Legendary, LoC::Damage, LoC::SlotArchery, 49608, 110),
		Item_Base(4846, LoC::Legendary, LoC::Damage, LoC::SlotThrowing, 49608, 110),
		Item_Base(4847, LoC::Legendary, LoC::Damage, LoC::SlotOneHP, 49608, 110),
		Item_Base(4848, LoC::Legendary, LoC::Damage, LoC::SlotTwoHP, 49608, 110),
		Item_Base(4849, LoC::Legendary, LoC::Damage, LoC::SlotShield, 49608, 110),
		Item_Base(4850, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 10),
		Item_Base(4851, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 10),
		Item_Base(4852, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 10),
		Item_Base(4853, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 10),
		Item_Base(4854, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 10),
		Item_Base(4855, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 10),
		Item_Base(4856, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 10),
		Item_Base(4857, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 10),
		Item_Base(4858, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 10),
		Item_Base(4859, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 10),
		Item_Base(4860, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 10),
		Item_Base(4861, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 10),
		Item_Base(4862, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 10),
		Item_Base(4863, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 10),
		Item_Base(4864, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 10),
		Item_Base(4865, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 10),
		Item_Base(4866, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 10),
		Item_Base(4867, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 10),
		Item_Base(4868, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 10),
		Item_Base(4869, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 10),
		Item_Base(4870, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 10),
		Item_Base(4871, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 10),
		Item_Base(4872, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 10),
		Item_Base(4873, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 10),
		Item_Base(4874, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 10),
		Item_Base(4875, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 20),
		Item_Base(4876, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 20),
		Item_Base(4877, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 20),
		Item_Base(4878, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 20),
		Item_Base(4879, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 20),
		Item_Base(4880, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 20),
		Item_Base(4881, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 20),
		Item_Base(4882, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 20),
		Item_Base(4883, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 20),
		Item_Base(4884, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 20),
		Item_Base(4885, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 20),
		Item_Base(4886, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 20),
		Item_Base(4887, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 20),
		Item_Base(4888, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 20),
		Item_Base(4889, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 20),
		Item_Base(4890, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 20),
		Item_Base(4891, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 20),
		Item_Base(4892, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 20),
		Item_Base(4893, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 20),
		Item_Base(4894, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 20),
		Item_Base(4895, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 20),
		Item_Base(4896, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 20),
		Item_Base(4897, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 20),
		Item_Base(4898, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 20),
		Item_Base(4899, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 20),
		Item_Base(4900, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 30),
		Item_Base(4901, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 30),
		Item_Base(4902, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 30),
		Item_Base(4903, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 30),
		Item_Base(4904, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 30),
		Item_Base(4905, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 30),
		Item_Base(4906, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 30),
		Item_Base(4907, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 30),
		Item_Base(4908, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 30),
		Item_Base(4909, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 30),
		Item_Base(4910, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 30),
		Item_Base(4911, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 30),
		Item_Base(4912, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 30),
		Item_Base(4913, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 30),
		Item_Base(4914, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 30),
		Item_Base(4915, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 30),
		Item_Base(4916, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 30),
		Item_Base(4917, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 30),
		Item_Base(4918, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 30),
		Item_Base(4919, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 30),
		Item_Base(4920, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 30),
		Item_Base(4921, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 30),
		Item_Base(4922, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 30),
		Item_Base(4923, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 30),
		Item_Base(4924, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 30),
		Item_Base(4925, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 40),
		Item_Base(4926, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 40),
		Item_Base(4927, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 40),
		Item_Base(4928, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 40),
		Item_Base(4929, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 40),
		Item_Base(4930, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 40),
		Item_Base(4931, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 40),
		Item_Base(4932, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 40),
		Item_Base(4933, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 40),
		Item_Base(4934, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 40),
		Item_Base(4935, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 40),
		Item_Base(4936, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 40),
		Item_Base(4937, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 40),
		Item_Base(4938, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 40),
		Item_Base(4939, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 40),
		Item_Base(4940, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 40),
		Item_Base(4941, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 40),
		Item_Base(4942, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 40),
		Item_Base(4943, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 40),
		Item_Base(4944, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 40),
		Item_Base(4945, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 40),
		Item_Base(4946, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 40),
		Item_Base(4947, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 40),
		Item_Base(4948, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 40),
		Item_Base(4949, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 40),
		Item_Base(4950, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 50),
		Item_Base(4951, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 50),
		Item_Base(4952, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 50),
		Item_Base(4953, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 50),
		Item_Base(4954, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 50),
		Item_Base(4955, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 50),
		Item_Base(4956, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 50),
		Item_Base(4957, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 50),
		Item_Base(4958, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 50),
		Item_Base(4959, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 50),
		Item_Base(4960, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 50),
		Item_Base(4961, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 50),
		Item_Base(4962, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 50),
		Item_Base(4963, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 50),
		Item_Base(4964, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 50),
		Item_Base(4965, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 50),
		Item_Base(4966, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 50),
		Item_Base(4967, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 50),
		Item_Base(4968, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 50),
		Item_Base(4969, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 50),
		Item_Base(4970, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 50),
		Item_Base(4971, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 50),
		Item_Base(4972, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 50),
		Item_Base(4973, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 50),
		Item_Base(4974, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 50),
		Item_Base(4975, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 60),
		Item_Base(4976, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 60),
		Item_Base(4977, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 60),
		Item_Base(4978, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 60),
		Item_Base(4979, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 60),
		Item_Base(4980, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 60),
		Item_Base(4981, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 60),
		Item_Base(4982, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 60),
		Item_Base(4983, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 60),
		Item_Base(4984, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 60),
		Item_Base(4985, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 60),
		Item_Base(4986, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 60),
		Item_Base(4987, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 60),
		Item_Base(4988, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 60),
		Item_Base(4989, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 60),
		Item_Base(4990, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 60),
		Item_Base(4991, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 60),
		Item_Base(4992, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 60),
		Item_Base(4993, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 60),
		Item_Base(4994, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 60),
		Item_Base(4995, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 60),
		Item_Base(4996, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 60),
		Item_Base(4997, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 60),
		Item_Base(4998, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 60),
		Item_Base(4999, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 60),
		Item_Base(5000, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 70),
		Item_Base(5001, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 70),
		Item_Base(5002, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 70),
		Item_Base(5003, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 70),
		Item_Base(5004, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 70),
		Item_Base(5005, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 70),
		Item_Base(5006, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 70),
		Item_Base(5007, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 70),
		Item_Base(5008, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 70),
		Item_Base(5009, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 70),
		Item_Base(5010, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 70),
		Item_Base(5011, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 70),
		Item_Base(5012, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 70),
		Item_Base(5013, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 70),
		Item_Base(5014, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 70),
		Item_Base(5015, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 70),
		Item_Base(5016, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 70),
		Item_Base(5017, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 70),
		Item_Base(5018, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 70),
		Item_Base(5019, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 70),
		Item_Base(5020, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 70),
		Item_Base(5021, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 70),
		Item_Base(5022, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 70),
		Item_Base(5023, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 70),
		Item_Base(5024, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 70),
		Item_Base(5025, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 80),
		Item_Base(5026, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 80),
		Item_Base(5027, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 80),
		Item_Base(5028, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 80),
		Item_Base(5029, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 80),
		Item_Base(5030, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 80),
		Item_Base(5031, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 80),
		Item_Base(5032, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 80),
		Item_Base(5033, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 80),
		Item_Base(5034, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 80),
		Item_Base(5035, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 80),
		Item_Base(5036, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 80),
		Item_Base(5037, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 80),
		Item_Base(5038, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 80),
		Item_Base(5039, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 80),
		Item_Base(5040, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 80),
		Item_Base(5041, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 80),
		Item_Base(5042, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 80),
		Item_Base(5043, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 80),
		Item_Base(5044, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 80),
		Item_Base(5045, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 80),
		Item_Base(5046, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 80),
		Item_Base(5047, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 80),
		Item_Base(5048, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 80),
		Item_Base(5049, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 80),
		Item_Base(5050, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 90),
		Item_Base(5051, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 90),
		Item_Base(5052, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 90),
		Item_Base(5053, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 90),
		Item_Base(5054, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 90),
		Item_Base(5055, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 90),
		Item_Base(5056, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 90),
		Item_Base(5057, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 90),
		Item_Base(5058, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 90),
		Item_Base(5059, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 90),
		Item_Base(5060, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 90),
		Item_Base(5061, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 90),
		Item_Base(5062, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 90),
		Item_Base(5063, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 90),
		Item_Base(5064, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 90),
		Item_Base(5065, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 90),
		Item_Base(5066, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 90),
		Item_Base(5067, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 90),
		Item_Base(5068, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 90),
		Item_Base(5069, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 90),
		Item_Base(5070, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 90),
		Item_Base(5071, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 90),
		Item_Base(5072, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 90),
		Item_Base(5073, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 90),
		Item_Base(5074, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 90),
		Item_Base(5075, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 100),
		Item_Base(5076, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 100),
		Item_Base(5077, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 100),
		Item_Base(5078, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 100),
		Item_Base(5079, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 100),
		Item_Base(5080, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 100),
		Item_Base(5081, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 100),
		Item_Base(5082, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 100),
		Item_Base(5083, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 100),
		Item_Base(5084, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 100),
		Item_Base(5085, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 100),
		Item_Base(5086, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 100),
		Item_Base(5087, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 100),
		Item_Base(5088, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 100),
		Item_Base(5089, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 100),
		Item_Base(5090, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 100),
		Item_Base(5091, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 100),
		Item_Base(5092, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 100),
		Item_Base(5093, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 100),
		Item_Base(5094, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 100),
		Item_Base(5095, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 100),
		Item_Base(5096, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 100),
		Item_Base(5097, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 100),
		Item_Base(5098, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 100),
		Item_Base(5099, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 100),
		Item_Base(5100, LoC::Unique, LoC::Damage, LoC::SlotEar, 49608, 110),
		Item_Base(5101, LoC::Unique, LoC::Damage, LoC::SlotNeck, 49608, 110),
		Item_Base(5102, LoC::Unique, LoC::Damage, LoC::SlotFace, 49608, 110),
		Item_Base(5103, LoC::Unique, LoC::Damage, LoC::SlotHead, 49608, 110),
		Item_Base(5104, LoC::Unique, LoC::Damage, LoC::SlotFingers, 49608, 110),
		Item_Base(5105, LoC::Unique, LoC::Damage, LoC::SlotWrist, 49608, 110),
		Item_Base(5106, LoC::Unique, LoC::Damage, LoC::SlotArms, 49608, 110),
		Item_Base(5107, LoC::Unique, LoC::Damage, LoC::SlotHand, 49608, 110),
		Item_Base(5108, LoC::Unique, LoC::Damage, LoC::SlotShoulders, 49608, 110),
		Item_Base(5109, LoC::Unique, LoC::Damage, LoC::SlotChest, 49608, 110),
		Item_Base(5110, LoC::Unique, LoC::Damage, LoC::SlotBack, 49608, 110),
		Item_Base(5111, LoC::Unique, LoC::Damage, LoC::SlotWaist, 49608, 110),
		Item_Base(5112, LoC::Unique, LoC::Damage, LoC::SlotLegs, 49608, 110),
		Item_Base(5113, LoC::Unique, LoC::Damage, LoC::SlotFeet, 49608, 110),
		Item_Base(5114, LoC::Unique, LoC::Damage, LoC::SlotCharm, 49608, 110),
		Item_Base(5115, LoC::Unique, LoC::Damage, LoC::SlotPowerSource, 49608, 110),
		Item_Base(5116, LoC::Unique, LoC::Damage, LoC::SlotOneHB, 49608, 110),
		Item_Base(5117, LoC::Unique, LoC::Damage, LoC::SlotTwoHB, 49608, 110),
		Item_Base(5118, LoC::Unique, LoC::Damage, LoC::SlotOneHS, 49608, 110),
		Item_Base(5119, LoC::Unique, LoC::Damage, LoC::SlotTwoHS, 49608, 110),
		Item_Base(5120, LoC::Unique, LoC::Damage, LoC::SlotArchery, 49608, 110),
		Item_Base(5121, LoC::Unique, LoC::Damage, LoC::SlotThrowing, 49608, 110),
		Item_Base(5122, LoC::Unique, LoC::Damage, LoC::SlotOneHP, 49608, 110),
		Item_Base(5123, LoC::Unique, LoC::Damage, LoC::SlotTwoHP, 49608, 110),
		Item_Base(5124, LoC::Unique, LoC::Damage, LoC::SlotShield, 49608, 110),
		Item_Base(5125, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 10),
		Item_Base(5126, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 10),
		Item_Base(5127, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 10),
		Item_Base(5128, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 10),
		Item_Base(5129, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 10),
		Item_Base(5130, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 10),
		Item_Base(5131, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 10),
		Item_Base(5132, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 10),
		Item_Base(5133, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 10),
		Item_Base(5134, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 10),
		Item_Base(5135, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 10),
		Item_Base(5136, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 10),
		Item_Base(5137, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 10),
		Item_Base(5138, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 10),
		Item_Base(5139, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 10),
		Item_Base(5140, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 10),
		Item_Base(5141, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 10),
		Item_Base(5142, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 10),
		Item_Base(5143, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 10),
		Item_Base(5144, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 10),
		Item_Base(5145, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 10),
		Item_Base(5146, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 10),
		Item_Base(5147, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 10),
		Item_Base(5148, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 10),
		Item_Base(5149, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 10),
		Item_Base(5150, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 20),
		Item_Base(5151, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 20),
		Item_Base(5152, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 20),
		Item_Base(5153, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 20),
		Item_Base(5154, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 20),
		Item_Base(5155, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 20),
		Item_Base(5156, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 20),
		Item_Base(5157, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 20),
		Item_Base(5158, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 20),
		Item_Base(5159, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 20),
		Item_Base(5160, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 20),
		Item_Base(5161, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 20),
		Item_Base(5162, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 20),
		Item_Base(5163, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 20),
		Item_Base(5164, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 20),
		Item_Base(5165, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 20),
		Item_Base(5166, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 20),
		Item_Base(5167, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 20),
		Item_Base(5168, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 20),
		Item_Base(5169, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 20),
		Item_Base(5170, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 20),
		Item_Base(5171, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 20),
		Item_Base(5172, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 20),
		Item_Base(5173, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 20),
		Item_Base(5174, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 20),
		Item_Base(5175, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 30),
		Item_Base(5176, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 30),
		Item_Base(5177, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 30),
		Item_Base(5178, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 30),
		Item_Base(5179, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 30),
		Item_Base(5180, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 30),
		Item_Base(5181, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 30),
		Item_Base(5182, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 30),
		Item_Base(5183, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 30),
		Item_Base(5184, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 30),
		Item_Base(5185, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 30),
		Item_Base(5186, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 30),
		Item_Base(5187, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 30),
		Item_Base(5188, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 30),
		Item_Base(5189, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 30),
		Item_Base(5190, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 30),
		Item_Base(5191, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 30),
		Item_Base(5192, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 30),
		Item_Base(5193, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 30),
		Item_Base(5194, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 30),
		Item_Base(5195, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 30),
		Item_Base(5196, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 30),
		Item_Base(5197, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 30),
		Item_Base(5198, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 30),
		Item_Base(5199, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 30),
		Item_Base(5200, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 40),
		Item_Base(5201, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 40),
		Item_Base(5202, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 40),
		Item_Base(5203, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 40),
		Item_Base(5204, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 40),
		Item_Base(5205, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 40),
		Item_Base(5206, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 40),
		Item_Base(5207, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 40),
		Item_Base(5208, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 40),
		Item_Base(5209, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 40),
		Item_Base(5210, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 40),
		Item_Base(5211, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 40),
		Item_Base(5212, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 40),
		Item_Base(5213, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 40),
		Item_Base(5214, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 40),
		Item_Base(5215, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 40),
		Item_Base(5216, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 40),
		Item_Base(5217, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 40),
		Item_Base(5218, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 40),
		Item_Base(5219, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 40),
		Item_Base(5220, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 40),
		Item_Base(5221, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 40),
		Item_Base(5222, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 40),
		Item_Base(5223, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 40),
		Item_Base(5224, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 40),
		Item_Base(5225, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 50),
		Item_Base(5226, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 50),
		Item_Base(5227, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 50),
		Item_Base(5228, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 50),
		Item_Base(5229, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 50),
		Item_Base(5230, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 50),
		Item_Base(5231, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 50),
		Item_Base(5232, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 50),
		Item_Base(5233, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 50),
		Item_Base(5234, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 50),
		Item_Base(5235, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 50),
		Item_Base(5236, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 50),
		Item_Base(5237, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 50),
		Item_Base(5238, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 50),
		Item_Base(5239, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 50),
		Item_Base(5240, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 50),
		Item_Base(5241, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 50),
		Item_Base(5242, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 50),
		Item_Base(5243, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 50),
		Item_Base(5244, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 50),
		Item_Base(5245, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 50),
		Item_Base(5246, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 50),
		Item_Base(5247, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 50),
		Item_Base(5248, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 50),
		Item_Base(5249, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 50),
		Item_Base(5250, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 60),
		Item_Base(5251, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 60),
		Item_Base(5252, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 60),
		Item_Base(5253, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 60),
		Item_Base(5254, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 60),
		Item_Base(5255, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 60),
		Item_Base(5256, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 60),
		Item_Base(5257, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 60),
		Item_Base(5258, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 60),
		Item_Base(5259, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 60),
		Item_Base(5260, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 60),
		Item_Base(5261, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 60),
		Item_Base(5262, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 60),
		Item_Base(5263, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 60),
		Item_Base(5264, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 60),
		Item_Base(5265, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 60),
		Item_Base(5266, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 60),
		Item_Base(5267, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 60),
		Item_Base(5268, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 60),
		Item_Base(5269, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 60),
		Item_Base(5270, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 60),
		Item_Base(5271, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 60),
		Item_Base(5272, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 60),
		Item_Base(5273, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 60),
		Item_Base(5274, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 60),
		Item_Base(5275, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 70),
		Item_Base(5276, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 70),
		Item_Base(5277, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 70),
		Item_Base(5278, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 70),
		Item_Base(5279, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 70),
		Item_Base(5280, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 70),
		Item_Base(5281, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 70),
		Item_Base(5282, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 70),
		Item_Base(5283, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 70),
		Item_Base(5284, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 70),
		Item_Base(5285, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 70),
		Item_Base(5286, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 70),
		Item_Base(5287, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 70),
		Item_Base(5288, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 70),
		Item_Base(5289, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 70),
		Item_Base(5290, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 70),
		Item_Base(5291, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 70),
		Item_Base(5292, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 70),
		Item_Base(5293, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 70),
		Item_Base(5294, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 70),
		Item_Base(5295, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 70),
		Item_Base(5296, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 70),
		Item_Base(5297, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 70),
		Item_Base(5298, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 70),
		Item_Base(5299, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 70),
		Item_Base(5300, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 80),
		Item_Base(5301, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 80),
		Item_Base(5302, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 80),
		Item_Base(5303, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 80),
		Item_Base(5304, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 80),
		Item_Base(5305, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 80),
		Item_Base(5306, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 80),
		Item_Base(5307, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 80),
		Item_Base(5308, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 80),
		Item_Base(5309, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 80),
		Item_Base(5310, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 80),
		Item_Base(5311, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 80),
		Item_Base(5312, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 80),
		Item_Base(5313, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 80),
		Item_Base(5314, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 80),
		Item_Base(5315, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 80),
		Item_Base(5316, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 80),
		Item_Base(5317, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 80),
		Item_Base(5318, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 80),
		Item_Base(5319, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 80),
		Item_Base(5320, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 80),
		Item_Base(5321, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 80),
		Item_Base(5322, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 80),
		Item_Base(5323, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 80),
		Item_Base(5324, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 80),
		Item_Base(5325, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 90),
		Item_Base(5326, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 90),
		Item_Base(5327, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 90),
		Item_Base(5328, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 90),
		Item_Base(5329, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 90),
		Item_Base(5330, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 90),
		Item_Base(5331, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 90),
		Item_Base(5332, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 90),
		Item_Base(5333, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 90),
		Item_Base(5334, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 90),
		Item_Base(5335, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 90),
		Item_Base(5336, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 90),
		Item_Base(5337, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 90),
		Item_Base(5338, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 90),
		Item_Base(5339, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 90),
		Item_Base(5340, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 90),
		Item_Base(5341, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 90),
		Item_Base(5342, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 90),
		Item_Base(5343, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 90),
		Item_Base(5344, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 90),
		Item_Base(5345, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 90),
		Item_Base(5346, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 90),
		Item_Base(5347, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 90),
		Item_Base(5348, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 90),
		Item_Base(5349, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 90),
		Item_Base(5350, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 100),
		Item_Base(5351, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 100),
		Item_Base(5352, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 100),
		Item_Base(5353, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 100),
		Item_Base(5354, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 100),
		Item_Base(5355, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 100),
		Item_Base(5356, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 100),
		Item_Base(5357, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 100),
		Item_Base(5358, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 100),
		Item_Base(5359, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 100),
		Item_Base(5360, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 100),
		Item_Base(5361, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 100),
		Item_Base(5362, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 100),
		Item_Base(5363, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 100),
		Item_Base(5364, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 100),
		Item_Base(5365, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 100),
		Item_Base(5366, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 100),
		Item_Base(5367, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 100),
		Item_Base(5368, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 100),
		Item_Base(5369, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 100),
		Item_Base(5370, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 100),
		Item_Base(5371, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 100),
		Item_Base(5372, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 100),
		Item_Base(5373, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 100),
		Item_Base(5374, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 100),
		Item_Base(5375, LoC::Common, LoC::Caster, LoC::SlotEar, 15360, 110),
		Item_Base(5376, LoC::Common, LoC::Caster, LoC::SlotNeck, 15360, 110),
		Item_Base(5377, LoC::Common, LoC::Caster, LoC::SlotFace, 15360, 110),
		Item_Base(5378, LoC::Common, LoC::Caster, LoC::SlotHead, 15360, 110),
		Item_Base(5379, LoC::Common, LoC::Caster, LoC::SlotFingers, 15360, 110),
		Item_Base(5380, LoC::Common, LoC::Caster, LoC::SlotWrist, 15360, 110),
		Item_Base(5381, LoC::Common, LoC::Caster, LoC::SlotArms, 15360, 110),
		Item_Base(5382, LoC::Common, LoC::Caster, LoC::SlotHand, 15360, 110),
		Item_Base(5383, LoC::Common, LoC::Caster, LoC::SlotShoulders, 15360, 110),
		Item_Base(5384, LoC::Common, LoC::Caster, LoC::SlotChest, 15360, 110),
		Item_Base(5385, LoC::Common, LoC::Caster, LoC::SlotBack, 15360, 110),
		Item_Base(5386, LoC::Common, LoC::Caster, LoC::SlotWaist, 15360, 110),
		Item_Base(5387, LoC::Common, LoC::Caster, LoC::SlotLegs, 15360, 110),
		Item_Base(5388, LoC::Common, LoC::Caster, LoC::SlotFeet, 15360, 110),
		Item_Base(5389, LoC::Common, LoC::Caster, LoC::SlotCharm, 15360, 110),
		Item_Base(5390, LoC::Common, LoC::Caster, LoC::SlotPowerSource, 15360, 110),
		Item_Base(5391, LoC::Common, LoC::Caster, LoC::SlotOneHB, 15360, 110),
		Item_Base(5392, LoC::Common, LoC::Caster, LoC::SlotTwoHB, 15360, 110),
		Item_Base(5393, LoC::Common, LoC::Caster, LoC::SlotOneHS, 15360, 110),
		Item_Base(5394, LoC::Common, LoC::Caster, LoC::SlotTwoHS, 15360, 110),
		Item_Base(5395, LoC::Common, LoC::Caster, LoC::SlotArchery, 15360, 110),
		Item_Base(5396, LoC::Common, LoC::Caster, LoC::SlotThrowing, 15360, 110),
		Item_Base(5397, LoC::Common, LoC::Caster, LoC::SlotOneHP, 15360, 110),
		Item_Base(5398, LoC::Common, LoC::Caster, LoC::SlotTwoHP, 15360, 110),
		Item_Base(5399, LoC::Common, LoC::Caster, LoC::SlotShield, 15360, 110),
		Item_Base(5400, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 10),
		Item_Base(5401, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 10),
		Item_Base(5402, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 10),
		Item_Base(5403, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 10),
		Item_Base(5404, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 10),
		Item_Base(5405, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 10),
		Item_Base(5406, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 10),
		Item_Base(5407, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 10),
		Item_Base(5408, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 10),
		Item_Base(5409, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 10),
		Item_Base(5410, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 10),
		Item_Base(5411, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 10),
		Item_Base(5412, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 10),
		Item_Base(5413, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 10),
		Item_Base(5414, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 10),
		Item_Base(5415, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 10),
		Item_Base(5416, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 10),
		Item_Base(5417, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 10),
		Item_Base(5418, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 10),
		Item_Base(5419, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 10),
		Item_Base(5420, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 10),
		Item_Base(5421, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 10),
		Item_Base(5422, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 10),
		Item_Base(5423, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 10),
		Item_Base(5424, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 10),
		Item_Base(5425, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 20),
		Item_Base(5426, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 20),
		Item_Base(5427, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 20),
		Item_Base(5428, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 20),
		Item_Base(5429, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 20),
		Item_Base(5430, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 20),
		Item_Base(5431, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 20),
		Item_Base(5432, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 20),
		Item_Base(5433, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 20),
		Item_Base(5434, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 20),
		Item_Base(5435, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 20),
		Item_Base(5436, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 20),
		Item_Base(5437, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 20),
		Item_Base(5438, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 20),
		Item_Base(5439, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 20),
		Item_Base(5440, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 20),
		Item_Base(5441, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 20),
		Item_Base(5442, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 20),
		Item_Base(5443, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 20),
		Item_Base(5444, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 20),
		Item_Base(5445, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 20),
		Item_Base(5446, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 20),
		Item_Base(5447, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 20),
		Item_Base(5448, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 20),
		Item_Base(5449, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 20),
		Item_Base(5450, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 30),
		Item_Base(5451, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 30),
		Item_Base(5452, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 30),
		Item_Base(5453, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 30),
		Item_Base(5454, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 30),
		Item_Base(5455, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 30),
		Item_Base(5456, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 30),
		Item_Base(5457, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 30),
		Item_Base(5458, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 30),
		Item_Base(5459, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 30),
		Item_Base(5460, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 30),
		Item_Base(5461, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 30),
		Item_Base(5462, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 30),
		Item_Base(5463, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 30),
		Item_Base(5464, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 30),
		Item_Base(5465, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 30),
		Item_Base(5466, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 30),
		Item_Base(5467, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 30),
		Item_Base(5468, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 30),
		Item_Base(5469, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 30),
		Item_Base(5470, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 30),
		Item_Base(5471, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 30),
		Item_Base(5472, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 30),
		Item_Base(5473, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 30),
		Item_Base(5474, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 30),
		Item_Base(5475, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 40),
		Item_Base(5476, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 40),
		Item_Base(5477, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 40),
		Item_Base(5478, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 40),
		Item_Base(5479, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 40),
		Item_Base(5480, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 40),
		Item_Base(5481, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 40),
		Item_Base(5482, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 40),
		Item_Base(5483, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 40),
		Item_Base(5484, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 40),
		Item_Base(5485, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 40),
		Item_Base(5486, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 40),
		Item_Base(5487, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 40),
		Item_Base(5488, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 40),
		Item_Base(5489, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 40),
		Item_Base(5490, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 40),
		Item_Base(5491, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 40),
		Item_Base(5492, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 40),
		Item_Base(5493, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 40),
		Item_Base(5494, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 40),
		Item_Base(5495, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 40),
		Item_Base(5496, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 40),
		Item_Base(5497, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 40),
		Item_Base(5498, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 40),
		Item_Base(5499, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 40),
		Item_Base(5500, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 50),
		Item_Base(5501, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 50),
		Item_Base(5502, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 50),
		Item_Base(5503, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 50),
		Item_Base(5504, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 50),
		Item_Base(5505, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 50),
		Item_Base(5506, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 50),
		Item_Base(5507, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 50),
		Item_Base(5508, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 50),
		Item_Base(5509, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 50),
		Item_Base(5510, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 50),
		Item_Base(5511, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 50),
		Item_Base(5512, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 50),
		Item_Base(5513, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 50),
		Item_Base(5514, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 50),
		Item_Base(5515, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 50),
		Item_Base(5516, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 50),
		Item_Base(5517, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 50),
		Item_Base(5518, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 50),
		Item_Base(5519, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 50),
		Item_Base(5520, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 50),
		Item_Base(5521, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 50),
		Item_Base(5522, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 50),
		Item_Base(5523, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 50),
		Item_Base(5524, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 50),
		Item_Base(5525, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 60),
		Item_Base(5526, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 60),
		Item_Base(5527, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 60),
		Item_Base(5528, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 60),
		Item_Base(5529, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 60),
		Item_Base(5530, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 60),
		Item_Base(5531, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 60),
		Item_Base(5532, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 60),
		Item_Base(5533, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 60),
		Item_Base(5534, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 60),
		Item_Base(5535, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 60),
		Item_Base(5536, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 60),
		Item_Base(5537, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 60),
		Item_Base(5538, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 60),
		Item_Base(5539, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 60),
		Item_Base(5540, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 60),
		Item_Base(5541, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 60),
		Item_Base(5542, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 60),
		Item_Base(5543, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 60),
		Item_Base(5544, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 60),
		Item_Base(5545, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 60),
		Item_Base(5546, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 60),
		Item_Base(5547, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 60),
		Item_Base(5548, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 60),
		Item_Base(5549, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 60),
		Item_Base(5550, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 70),
		Item_Base(5551, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 70),
		Item_Base(5552, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 70),
		Item_Base(5553, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 70),
		Item_Base(5554, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 70),
		Item_Base(5555, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 70),
		Item_Base(5556, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 70),
		Item_Base(5557, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 70),
		Item_Base(5558, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 70),
		Item_Base(5559, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 70),
		Item_Base(5560, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 70),
		Item_Base(5561, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 70),
		Item_Base(5562, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 70),
		Item_Base(5563, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 70),
		Item_Base(5564, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 70),
		Item_Base(5565, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 70),
		Item_Base(5566, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 70),
		Item_Base(5567, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 70),
		Item_Base(5568, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 70),
		Item_Base(5569, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 70),
		Item_Base(5570, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 70),
		Item_Base(5571, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 70),
		Item_Base(5572, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 70),
		Item_Base(5573, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 70),
		Item_Base(5574, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 70),
		Item_Base(5575, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 80),
		Item_Base(5576, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 80),
		Item_Base(5577, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 80),
		Item_Base(5578, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 80),
		Item_Base(5579, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 80),
		Item_Base(5580, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 80),
		Item_Base(5581, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 80),
		Item_Base(5582, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 80),
		Item_Base(5583, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 80),
		Item_Base(5584, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 80),
		Item_Base(5585, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 80),
		Item_Base(5586, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 80),
		Item_Base(5587, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 80),
		Item_Base(5588, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 80),
		Item_Base(5589, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 80),
		Item_Base(5590, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 80),
		Item_Base(5591, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 80),
		Item_Base(5592, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 80),
		Item_Base(5593, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 80),
		Item_Base(5594, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 80),
		Item_Base(5595, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 80),
		Item_Base(5596, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 80),
		Item_Base(5597, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 80),
		Item_Base(5598, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 80),
		Item_Base(5599, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 80),
		Item_Base(5600, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 90),
		Item_Base(5601, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 90),
		Item_Base(5602, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 90),
		Item_Base(5603, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 90),
		Item_Base(5604, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 90),
		Item_Base(5605, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 90),
		Item_Base(5606, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 90),
		Item_Base(5607, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 90),
		Item_Base(5608, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 90),
		Item_Base(5609, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 90),
		Item_Base(5610, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 90),
		Item_Base(5611, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 90),
		Item_Base(5612, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 90),
		Item_Base(5613, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 90),
		Item_Base(5614, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 90),
		Item_Base(5615, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 90),
		Item_Base(5616, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 90),
		Item_Base(5617, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 90),
		Item_Base(5618, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 90),
		Item_Base(5619, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 90),
		Item_Base(5620, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 90),
		Item_Base(5621, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 90),
		Item_Base(5622, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 90),
		Item_Base(5623, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 90),
		Item_Base(5624, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 90),
		Item_Base(5625, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 100),
		Item_Base(5626, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 100),
		Item_Base(5627, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 100),
		Item_Base(5628, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 100),
		Item_Base(5629, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 100),
		Item_Base(5630, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 100),
		Item_Base(5631, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 100),
		Item_Base(5632, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 100),
		Item_Base(5633, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 100),
		Item_Base(5634, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 100),
		Item_Base(5635, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 100),
		Item_Base(5636, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 100),
		Item_Base(5637, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 100),
		Item_Base(5638, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 100),
		Item_Base(5639, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 100),
		Item_Base(5640, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 100),
		Item_Base(5641, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 100),
		Item_Base(5642, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 100),
		Item_Base(5643, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 100),
		Item_Base(5644, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 100),
		Item_Base(5645, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 100),
		Item_Base(5646, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 100),
		Item_Base(5647, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 100),
		Item_Base(5648, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 100),
		Item_Base(5649, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 100),
		Item_Base(5650, LoC::Uncommon, LoC::Caster, LoC::SlotEar, 15360, 110),
		Item_Base(5651, LoC::Uncommon, LoC::Caster, LoC::SlotNeck, 15360, 110),
		Item_Base(5652, LoC::Uncommon, LoC::Caster, LoC::SlotFace, 15360, 110),
		Item_Base(5653, LoC::Uncommon, LoC::Caster, LoC::SlotHead, 15360, 110),
		Item_Base(5654, LoC::Uncommon, LoC::Caster, LoC::SlotFingers, 15360, 110),
		Item_Base(5655, LoC::Uncommon, LoC::Caster, LoC::SlotWrist, 15360, 110),
		Item_Base(5656, LoC::Uncommon, LoC::Caster, LoC::SlotArms, 15360, 110),
		Item_Base(5657, LoC::Uncommon, LoC::Caster, LoC::SlotHand, 15360, 110),
		Item_Base(5658, LoC::Uncommon, LoC::Caster, LoC::SlotShoulders, 15360, 110),
		Item_Base(5659, LoC::Uncommon, LoC::Caster, LoC::SlotChest, 15360, 110),
		Item_Base(5660, LoC::Uncommon, LoC::Caster, LoC::SlotBack, 15360, 110),
		Item_Base(5661, LoC::Uncommon, LoC::Caster, LoC::SlotWaist, 15360, 110),
		Item_Base(5662, LoC::Uncommon, LoC::Caster, LoC::SlotLegs, 15360, 110),
		Item_Base(5663, LoC::Uncommon, LoC::Caster, LoC::SlotFeet, 15360, 110),
		Item_Base(5664, LoC::Uncommon, LoC::Caster, LoC::SlotCharm, 15360, 110),
		Item_Base(5665, LoC::Uncommon, LoC::Caster, LoC::SlotPowerSource, 15360, 110),
		Item_Base(5666, LoC::Uncommon, LoC::Caster, LoC::SlotOneHB, 15360, 110),
		Item_Base(5667, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHB, 15360, 110),
		Item_Base(5668, LoC::Uncommon, LoC::Caster, LoC::SlotOneHS, 15360, 110),
		Item_Base(5669, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHS, 15360, 110),
		Item_Base(5670, LoC::Uncommon, LoC::Caster, LoC::SlotArchery, 15360, 110),
		Item_Base(5671, LoC::Uncommon, LoC::Caster, LoC::SlotThrowing, 15360, 110),
		Item_Base(5672, LoC::Uncommon, LoC::Caster, LoC::SlotOneHP, 15360, 110),
		Item_Base(5673, LoC::Uncommon, LoC::Caster, LoC::SlotTwoHP, 15360, 110),
		Item_Base(5674, LoC::Uncommon, LoC::Caster, LoC::SlotShield, 15360, 110),
		Item_Base(5675, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 10),
		Item_Base(5676, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 10),
		Item_Base(5677, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 10),
		Item_Base(5678, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 10),
		Item_Base(5679, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 10),
		Item_Base(5680, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 10),
		Item_Base(5681, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 10),
		Item_Base(5682, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 10),
		Item_Base(5683, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 10),
		Item_Base(5684, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 10),
		Item_Base(5685, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 10),
		Item_Base(5686, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 10),
		Item_Base(5687, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 10),
		Item_Base(5688, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 10),
		Item_Base(5689, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 10),
		Item_Base(5690, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 10),
		Item_Base(5691, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 10),
		Item_Base(5692, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 10),
		Item_Base(5693, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 10),
		Item_Base(5694, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 10),
		Item_Base(5695, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 10),
		Item_Base(5696, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 10),
		Item_Base(5697, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 10),
		Item_Base(5698, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 10),
		Item_Base(5699, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 10),
		Item_Base(5700, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 20),
		Item_Base(5701, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 20),
		Item_Base(5702, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 20),
		Item_Base(5703, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 20),
		Item_Base(5704, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 20),
		Item_Base(5705, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 20),
		Item_Base(5706, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 20),
		Item_Base(5707, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 20),
		Item_Base(5708, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 20),
		Item_Base(5709, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 20),
		Item_Base(5710, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 20),
		Item_Base(5711, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 20),
		Item_Base(5712, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 20),
		Item_Base(5713, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 20),
		Item_Base(5714, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 20),
		Item_Base(5715, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 20),
		Item_Base(5716, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 20),
		Item_Base(5717, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 20),
		Item_Base(5718, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 20),
		Item_Base(5719, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 20),
		Item_Base(5720, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 20),
		Item_Base(5721, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 20),
		Item_Base(5722, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 20),
		Item_Base(5723, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 20),
		Item_Base(5724, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 20),
		Item_Base(5725, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 30),
		Item_Base(5726, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 30),
		Item_Base(5727, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 30),
		Item_Base(5728, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 30),
		Item_Base(5729, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 30),
		Item_Base(5730, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 30),
		Item_Base(5731, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 30),
		Item_Base(5732, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 30),
		Item_Base(5733, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 30),
		Item_Base(5734, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 30),
		Item_Base(5735, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 30),
		Item_Base(5736, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 30),
		Item_Base(5737, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 30),
		Item_Base(5738, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 30),
		Item_Base(5739, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 30),
		Item_Base(5740, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 30),
		Item_Base(5741, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 30),
		Item_Base(5742, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 30),
		Item_Base(5743, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 30),
		Item_Base(5744, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 30),
		Item_Base(5745, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 30),
		Item_Base(5746, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 30),
		Item_Base(5747, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 30),
		Item_Base(5748, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 30),
		Item_Base(5749, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 30),
		Item_Base(5750, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 40),
		Item_Base(5751, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 40),
		Item_Base(5752, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 40),
		Item_Base(5753, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 40),
		Item_Base(5754, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 40),
		Item_Base(5755, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 40),
		Item_Base(5756, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 40),
		Item_Base(5757, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 40),
		Item_Base(5758, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 40),
		Item_Base(5759, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 40),
		Item_Base(5760, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 40),
		Item_Base(5761, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 40),
		Item_Base(5762, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 40),
		Item_Base(5763, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 40),
		Item_Base(5764, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 40),
		Item_Base(5765, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 40),
		Item_Base(5766, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 40),
		Item_Base(5767, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 40),
		Item_Base(5768, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 40),
		Item_Base(5769, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 40),
		Item_Base(5770, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 40),
		Item_Base(5771, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 40),
		Item_Base(5772, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 40),
		Item_Base(5773, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 40),
		Item_Base(5774, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 40),
		Item_Base(5775, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 50),
		Item_Base(5776, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 50),
		Item_Base(5777, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 50),
		Item_Base(5778, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 50),
		Item_Base(5779, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 50),
		Item_Base(5780, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 50),
		Item_Base(5781, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 50),
		Item_Base(5782, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 50),
		Item_Base(5783, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 50),
		Item_Base(5784, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 50),
		Item_Base(5785, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 50),
		Item_Base(5786, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 50),
		Item_Base(5787, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 50),
		Item_Base(5788, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 50),
		Item_Base(5789, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 50),
		Item_Base(5790, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 50),
		Item_Base(5791, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 50),
		Item_Base(5792, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 50),
		Item_Base(5793, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 50),
		Item_Base(5794, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 50),
		Item_Base(5795, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 50),
		Item_Base(5796, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 50),
		Item_Base(5797, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 50),
		Item_Base(5798, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 50),
		Item_Base(5799, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 50),
		Item_Base(5800, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 60),
		Item_Base(5801, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 60),
		Item_Base(5802, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 60),
		Item_Base(5803, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 60),
		Item_Base(5804, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 60),
		Item_Base(5805, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 60),
		Item_Base(5806, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 60),
		Item_Base(5807, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 60),
		Item_Base(5808, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 60),
		Item_Base(5809, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 60),
		Item_Base(5810, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 60),
		Item_Base(5811, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 60),
		Item_Base(5812, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 60),
		Item_Base(5813, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 60),
		Item_Base(5814, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 60),
		Item_Base(5815, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 60),
		Item_Base(5816, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 60),
		Item_Base(5817, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 60),
		Item_Base(5818, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 60),
		Item_Base(5819, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 60),
		Item_Base(5820, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 60),
		Item_Base(5821, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 60),
		Item_Base(5822, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 60),
		Item_Base(5823, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 60),
		Item_Base(5824, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 60),
		Item_Base(5825, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 70),
		Item_Base(5826, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 70),
		Item_Base(5827, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 70),
		Item_Base(5828, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 70),
		Item_Base(5829, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 70),
		Item_Base(5830, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 70),
		Item_Base(5831, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 70),
		Item_Base(5832, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 70),
		Item_Base(5833, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 70),
		Item_Base(5834, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 70),
		Item_Base(5835, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 70),
		Item_Base(5836, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 70),
		Item_Base(5837, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 70),
		Item_Base(5838, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 70),
		Item_Base(5839, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 70),
		Item_Base(5840, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 70),
		Item_Base(5841, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 70),
		Item_Base(5842, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 70),
		Item_Base(5843, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 70),
		Item_Base(5844, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 70),
		Item_Base(5845, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 70),
		Item_Base(5846, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 70),
		Item_Base(5847, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 70),
		Item_Base(5848, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 70),
		Item_Base(5849, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 70),
		Item_Base(5850, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 80),
		Item_Base(5851, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 80),
		Item_Base(5852, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 80),
		Item_Base(5853, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 80),
		Item_Base(5854, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 80),
		Item_Base(5855, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 80),
		Item_Base(5856, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 80),
		Item_Base(5857, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 80),
		Item_Base(5858, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 80),
		Item_Base(5859, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 80),
		Item_Base(5860, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 80),
		Item_Base(5861, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 80),
		Item_Base(5862, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 80),
		Item_Base(5863, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 80),
		Item_Base(5864, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 80),
		Item_Base(5865, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 80),
		Item_Base(5866, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 80),
		Item_Base(5867, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 80),
		Item_Base(5868, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 80),
		Item_Base(5869, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 80),
		Item_Base(5870, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 80),
		Item_Base(5871, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 80),
		Item_Base(5872, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 80),
		Item_Base(5873, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 80),
		Item_Base(5874, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 80),
		Item_Base(5875, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 90),
		Item_Base(5876, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 90),
		Item_Base(5877, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 90),
		Item_Base(5878, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 90),
		Item_Base(5879, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 90),
		Item_Base(5880, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 90),
		Item_Base(5881, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 90),
		Item_Base(5882, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 90),
		Item_Base(5883, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 90),
		Item_Base(5884, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 90),
		Item_Base(5885, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 90),
		Item_Base(5886, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 90),
		Item_Base(5887, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 90),
		Item_Base(5888, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 90),
		Item_Base(5889, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 90),
		Item_Base(5890, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 90),
		Item_Base(5891, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 90),
		Item_Base(5892, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 90),
		Item_Base(5893, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 90),
		Item_Base(5894, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 90),
		Item_Base(5895, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 90),
		Item_Base(5896, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 90),
		Item_Base(5897, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 90),
		Item_Base(5898, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 90),
		Item_Base(5899, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 90),
		Item_Base(5900, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 100),
		Item_Base(5901, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 100),
		Item_Base(5902, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 100),
		Item_Base(5903, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 100),
		Item_Base(5904, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 100),
		Item_Base(5905, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 100),
		Item_Base(5906, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 100),
		Item_Base(5907, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 100),
		Item_Base(5908, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 100),
		Item_Base(5909, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 100),
		Item_Base(5910, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 100),
		Item_Base(5911, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 100),
		Item_Base(5912, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 100),
		Item_Base(5913, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 100),
		Item_Base(5914, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 100),
		Item_Base(5915, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 100),
		Item_Base(5916, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 100),
		Item_Base(5917, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 100),
		Item_Base(5918, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 100),
		Item_Base(5919, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 100),
		Item_Base(5920, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 100),
		Item_Base(5921, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 100),
		Item_Base(5922, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 100),
		Item_Base(5923, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 100),
		Item_Base(5924, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 100),
		Item_Base(5925, LoC::Rare, LoC::Caster, LoC::SlotEar, 15360, 110),
		Item_Base(5926, LoC::Rare, LoC::Caster, LoC::SlotNeck, 15360, 110),
		Item_Base(5927, LoC::Rare, LoC::Caster, LoC::SlotFace, 15360, 110),
		Item_Base(5928, LoC::Rare, LoC::Caster, LoC::SlotHead, 15360, 110),
		Item_Base(5929, LoC::Rare, LoC::Caster, LoC::SlotFingers, 15360, 110),
		Item_Base(5930, LoC::Rare, LoC::Caster, LoC::SlotWrist, 15360, 110),
		Item_Base(5931, LoC::Rare, LoC::Caster, LoC::SlotArms, 15360, 110),
		Item_Base(5932, LoC::Rare, LoC::Caster, LoC::SlotHand, 15360, 110),
		Item_Base(5933, LoC::Rare, LoC::Caster, LoC::SlotShoulders, 15360, 110),
		Item_Base(5934, LoC::Rare, LoC::Caster, LoC::SlotChest, 15360, 110),
		Item_Base(5935, LoC::Rare, LoC::Caster, LoC::SlotBack, 15360, 110),
		Item_Base(5936, LoC::Rare, LoC::Caster, LoC::SlotWaist, 15360, 110),
		Item_Base(5937, LoC::Rare, LoC::Caster, LoC::SlotLegs, 15360, 110),
		Item_Base(5938, LoC::Rare, LoC::Caster, LoC::SlotFeet, 15360, 110),
		Item_Base(5939, LoC::Rare, LoC::Caster, LoC::SlotCharm, 15360, 110),
		Item_Base(5940, LoC::Rare, LoC::Caster, LoC::SlotPowerSource, 15360, 110),
		Item_Base(5941, LoC::Rare, LoC::Caster, LoC::SlotOneHB, 15360, 110),
		Item_Base(5942, LoC::Rare, LoC::Caster, LoC::SlotTwoHB, 15360, 110),
		Item_Base(5943, LoC::Rare, LoC::Caster, LoC::SlotOneHS, 15360, 110),
		Item_Base(5944, LoC::Rare, LoC::Caster, LoC::SlotTwoHS, 15360, 110),
		Item_Base(5945, LoC::Rare, LoC::Caster, LoC::SlotArchery, 15360, 110),
		Item_Base(5946, LoC::Rare, LoC::Caster, LoC::SlotThrowing, 15360, 110),
		Item_Base(5947, LoC::Rare, LoC::Caster, LoC::SlotOneHP, 15360, 110),
		Item_Base(5948, LoC::Rare, LoC::Caster, LoC::SlotTwoHP, 15360, 110),
		Item_Base(5949, LoC::Rare, LoC::Caster, LoC::SlotShield, 15360, 110),
		Item_Base(5950, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 10),
		Item_Base(5951, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 10),
		Item_Base(5952, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 10),
		Item_Base(5953, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 10),
		Item_Base(5954, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 10),
		Item_Base(5955, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 10),
		Item_Base(5956, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 10),
		Item_Base(5957, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 10),
		Item_Base(5958, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 10),
		Item_Base(5959, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 10),
		Item_Base(5960, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 10),
		Item_Base(5961, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 10),
		Item_Base(5962, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 10),
		Item_Base(5963, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 10),
		Item_Base(5964, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 10),
		Item_Base(5965, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 10),
		Item_Base(5966, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 10),
		Item_Base(5967, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 10),
		Item_Base(5968, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 10),
		Item_Base(5969, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 10),
		Item_Base(5970, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 10),
		Item_Base(5971, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 10),
		Item_Base(5972, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 10),
		Item_Base(5973, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 10),
		Item_Base(5974, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 10),
		Item_Base(5975, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 20),
		Item_Base(5976, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 20),
		Item_Base(5977, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 20),
		Item_Base(5978, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 20),
		Item_Base(5979, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 20),
		Item_Base(5980, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 20),
		Item_Base(5981, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 20),
		Item_Base(5982, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 20),
		Item_Base(5983, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 20),
		Item_Base(5984, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 20),
		Item_Base(5985, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 20),
		Item_Base(5986, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 20),
		Item_Base(5987, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 20),
		Item_Base(5988, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 20),
		Item_Base(5989, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 20),
		Item_Base(5990, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 20),
		Item_Base(5991, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 20),
		Item_Base(5992, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 20),
		Item_Base(5993, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 20),
		Item_Base(5994, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 20),
		Item_Base(5995, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 20),
		Item_Base(5996, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 20),
		Item_Base(5997, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 20),
		Item_Base(5998, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 20),
		Item_Base(5999, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 20),
		Item_Base(6000, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 30),
		Item_Base(6001, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 30),
		Item_Base(6002, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 30),
		Item_Base(6003, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 30),
		Item_Base(6004, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 30),
		Item_Base(6005, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 30),
		Item_Base(6006, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 30),
		Item_Base(6007, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 30),
		Item_Base(6008, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 30),
		Item_Base(6009, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 30),
		Item_Base(6010, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 30),
		Item_Base(6011, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 30),
		Item_Base(6012, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 30),
		Item_Base(6013, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 30),
		Item_Base(6014, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 30),
		Item_Base(6015, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 30),
		Item_Base(6016, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 30),
		Item_Base(6017, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 30),
		Item_Base(6018, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 30),
		Item_Base(6019, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 30),
		Item_Base(6020, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 30),
		Item_Base(6021, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 30),
		Item_Base(6022, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 30),
		Item_Base(6023, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 30),
		Item_Base(6024, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 30),
		Item_Base(6025, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 40),
		Item_Base(6026, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 40),
		Item_Base(6027, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 40),
		Item_Base(6028, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 40),
		Item_Base(6029, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 40),
		Item_Base(6030, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 40),
		Item_Base(6031, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 40),
		Item_Base(6032, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 40),
		Item_Base(6033, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 40),
		Item_Base(6034, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 40),
		Item_Base(6035, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 40),
		Item_Base(6036, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 40),
		Item_Base(6037, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 40),
		Item_Base(6038, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 40),
		Item_Base(6039, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 40),
		Item_Base(6040, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 40),
		Item_Base(6041, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 40),
		Item_Base(6042, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 40),
		Item_Base(6043, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 40),
		Item_Base(6044, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 40),
		Item_Base(6045, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 40),
		Item_Base(6046, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 40),
		Item_Base(6047, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 40),
		Item_Base(6048, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 40),
		Item_Base(6049, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 40),
		Item_Base(6050, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 50),
		Item_Base(6051, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 50),
		Item_Base(6052, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 50),
		Item_Base(6053, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 50),
		Item_Base(6054, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 50),
		Item_Base(6055, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 50),
		Item_Base(6056, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 50),
		Item_Base(6057, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 50),
		Item_Base(6058, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 50),
		Item_Base(6059, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 50),
		Item_Base(6060, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 50),
		Item_Base(6061, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 50),
		Item_Base(6062, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 50),
		Item_Base(6063, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 50),
		Item_Base(6064, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 50),
		Item_Base(6065, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 50),
		Item_Base(6066, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 50),
		Item_Base(6067, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 50),
		Item_Base(6068, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 50),
		Item_Base(6069, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 50),
		Item_Base(6070, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 50),
		Item_Base(6071, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 50),
		Item_Base(6072, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 50),
		Item_Base(6073, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 50),
		Item_Base(6074, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 50),
		Item_Base(6075, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 60),
		Item_Base(6076, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 60),
		Item_Base(6077, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 60),
		Item_Base(6078, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 60),
		Item_Base(6079, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 60),
		Item_Base(6080, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 60),
		Item_Base(6081, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 60),
		Item_Base(6082, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 60),
		Item_Base(6083, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 60),
		Item_Base(6084, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 60),
		Item_Base(6085, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 60),
		Item_Base(6086, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 60),
		Item_Base(6087, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 60),
		Item_Base(6088, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 60),
		Item_Base(6089, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 60),
		Item_Base(6090, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 60),
		Item_Base(6091, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 60),
		Item_Base(6092, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 60),
		Item_Base(6093, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 60),
		Item_Base(6094, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 60),
		Item_Base(6095, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 60),
		Item_Base(6096, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 60),
		Item_Base(6097, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 60),
		Item_Base(6098, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 60),
		Item_Base(6099, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 60),
		Item_Base(6100, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 70),
		Item_Base(6101, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 70),
		Item_Base(6102, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 70),
		Item_Base(6103, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 70),
		Item_Base(6104, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 70),
		Item_Base(6105, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 70),
		Item_Base(6106, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 70),
		Item_Base(6107, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 70),
		Item_Base(6108, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 70),
		Item_Base(6109, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 70),
		Item_Base(6110, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 70),
		Item_Base(6111, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 70),
		Item_Base(6112, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 70),
		Item_Base(6113, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 70),
		Item_Base(6114, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 70),
		Item_Base(6115, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 70),
		Item_Base(6116, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 70),
		Item_Base(6117, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 70),
		Item_Base(6118, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 70),
		Item_Base(6119, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 70),
		Item_Base(6120, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 70),
		Item_Base(6121, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 70),
		Item_Base(6122, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 70),
		Item_Base(6123, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 70),
		Item_Base(6124, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 70),
		Item_Base(6125, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 80),
		Item_Base(6126, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 80),
		Item_Base(6127, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 80),
		Item_Base(6128, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 80),
		Item_Base(6129, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 80),
		Item_Base(6130, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 80),
		Item_Base(6131, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 80),
		Item_Base(6132, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 80),
		Item_Base(6133, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 80),
		Item_Base(6134, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 80),
		Item_Base(6135, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 80),
		Item_Base(6136, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 80),
		Item_Base(6137, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 80),
		Item_Base(6138, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 80),
		Item_Base(6139, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 80),
		Item_Base(6140, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 80),
		Item_Base(6141, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 80),
		Item_Base(6142, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 80),
		Item_Base(6143, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 80),
		Item_Base(6144, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 80),
		Item_Base(6145, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 80),
		Item_Base(6146, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 80),
		Item_Base(6147, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 80),
		Item_Base(6148, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 80),
		Item_Base(6149, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 80),
		Item_Base(6150, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 90),
		Item_Base(6151, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 90),
		Item_Base(6152, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 90),
		Item_Base(6153, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 90),
		Item_Base(6154, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 90),
		Item_Base(6155, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 90),
		Item_Base(6156, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 90),
		Item_Base(6157, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 90),
		Item_Base(6158, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 90),
		Item_Base(6159, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 90),
		Item_Base(6160, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 90),
		Item_Base(6161, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 90),
		Item_Base(6162, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 90),
		Item_Base(6163, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 90),
		Item_Base(6164, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 90),
		Item_Base(6165, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 90),
		Item_Base(6166, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 90),
		Item_Base(6167, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 90),
		Item_Base(6168, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 90),
		Item_Base(6169, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 90),
		Item_Base(6170, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 90),
		Item_Base(6171, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 90),
		Item_Base(6172, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 90),
		Item_Base(6173, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 90),
		Item_Base(6174, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 90),
		Item_Base(6175, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 100),
		Item_Base(6176, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 100),
		Item_Base(6177, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 100),
		Item_Base(6178, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 100),
		Item_Base(6179, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 100),
		Item_Base(6180, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 100),
		Item_Base(6181, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 100),
		Item_Base(6182, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 100),
		Item_Base(6183, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 100),
		Item_Base(6184, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 100),
		Item_Base(6185, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 100),
		Item_Base(6186, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 100),
		Item_Base(6187, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 100),
		Item_Base(6188, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 100),
		Item_Base(6189, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 100),
		Item_Base(6190, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 100),
		Item_Base(6191, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 100),
		Item_Base(6192, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 100),
		Item_Base(6193, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 100),
		Item_Base(6194, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 100),
		Item_Base(6195, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 100),
		Item_Base(6196, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 100),
		Item_Base(6197, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 100),
		Item_Base(6198, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 100),
		Item_Base(6199, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 100),
		Item_Base(6200, LoC::Legendary, LoC::Caster, LoC::SlotEar, 15360, 110),
		Item_Base(6201, LoC::Legendary, LoC::Caster, LoC::SlotNeck, 15360, 110),
		Item_Base(6202, LoC::Legendary, LoC::Caster, LoC::SlotFace, 15360, 110),
		Item_Base(6203, LoC::Legendary, LoC::Caster, LoC::SlotHead, 15360, 110),
		Item_Base(6204, LoC::Legendary, LoC::Caster, LoC::SlotFingers, 15360, 110),
		Item_Base(6205, LoC::Legendary, LoC::Caster, LoC::SlotWrist, 15360, 110),
		Item_Base(6206, LoC::Legendary, LoC::Caster, LoC::SlotArms, 15360, 110),
		Item_Base(6207, LoC::Legendary, LoC::Caster, LoC::SlotHand, 15360, 110),
		Item_Base(6208, LoC::Legendary, LoC::Caster, LoC::SlotShoulders, 15360, 110),
		Item_Base(6209, LoC::Legendary, LoC::Caster, LoC::SlotChest, 15360, 110),
		Item_Base(6210, LoC::Legendary, LoC::Caster, LoC::SlotBack, 15360, 110),
		Item_Base(6211, LoC::Legendary, LoC::Caster, LoC::SlotWaist, 15360, 110),
		Item_Base(6212, LoC::Legendary, LoC::Caster, LoC::SlotLegs, 15360, 110),
		Item_Base(6213, LoC::Legendary, LoC::Caster, LoC::SlotFeet, 15360, 110),
		Item_Base(6214, LoC::Legendary, LoC::Caster, LoC::SlotCharm, 15360, 110),
		Item_Base(6215, LoC::Legendary, LoC::Caster, LoC::SlotPowerSource, 15360, 110),
		Item_Base(6216, LoC::Legendary, LoC::Caster, LoC::SlotOneHB, 15360, 110),
		Item_Base(6217, LoC::Legendary, LoC::Caster, LoC::SlotTwoHB, 15360, 110),
		Item_Base(6218, LoC::Legendary, LoC::Caster, LoC::SlotOneHS, 15360, 110),
		Item_Base(6219, LoC::Legendary, LoC::Caster, LoC::SlotTwoHS, 15360, 110),
		Item_Base(6220, LoC::Legendary, LoC::Caster, LoC::SlotArchery, 15360, 110),
		Item_Base(6221, LoC::Legendary, LoC::Caster, LoC::SlotThrowing, 15360, 110),
		Item_Base(6222, LoC::Legendary, LoC::Caster, LoC::SlotOneHP, 15360, 110),
		Item_Base(6223, LoC::Legendary, LoC::Caster, LoC::SlotTwoHP, 15360, 110),
		Item_Base(6224, LoC::Legendary, LoC::Caster, LoC::SlotShield, 15360, 110),
		Item_Base(6225, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 10),
		Item_Base(6226, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 10),
		Item_Base(6227, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 10),
		Item_Base(6228, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 10),
		Item_Base(6229, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 10),
		Item_Base(6230, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 10),
		Item_Base(6231, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 10),
		Item_Base(6232, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 10),
		Item_Base(6233, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 10),
		Item_Base(6234, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 10),
		Item_Base(6235, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 10),
		Item_Base(6236, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 10),
		Item_Base(6237, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 10),
		Item_Base(6238, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 10),
		Item_Base(6239, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 10),
		Item_Base(6240, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 10),
		Item_Base(6241, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 10),
		Item_Base(6242, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 10),
		Item_Base(6243, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 10),
		Item_Base(6244, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 10),
		Item_Base(6245, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 10),
		Item_Base(6246, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 10),
		Item_Base(6247, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 10),
		Item_Base(6248, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 10),
		Item_Base(6249, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 10),
		Item_Base(6250, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 20),
		Item_Base(6251, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 20),
		Item_Base(6252, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 20),
		Item_Base(6253, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 20),
		Item_Base(6254, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 20),
		Item_Base(6255, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 20),
		Item_Base(6256, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 20),
		Item_Base(6257, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 20),
		Item_Base(6258, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 20),
		Item_Base(6259, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 20),
		Item_Base(6260, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 20),
		Item_Base(6261, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 20),
		Item_Base(6262, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 20),
		Item_Base(6263, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 20),
		Item_Base(6264, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 20),
		Item_Base(6265, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 20),
		Item_Base(6266, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 20),
		Item_Base(6267, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 20),
		Item_Base(6268, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 20),
		Item_Base(6269, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 20),
		Item_Base(6270, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 20),
		Item_Base(6271, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 20),
		Item_Base(6272, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 20),
		Item_Base(6273, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 20),
		Item_Base(6274, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 20),
		Item_Base(6275, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 30),
		Item_Base(6276, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 30),
		Item_Base(6277, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 30),
		Item_Base(6278, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 30),
		Item_Base(6279, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 30),
		Item_Base(6280, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 30),
		Item_Base(6281, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 30),
		Item_Base(6282, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 30),
		Item_Base(6283, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 30),
		Item_Base(6284, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 30),
		Item_Base(6285, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 30),
		Item_Base(6286, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 30),
		Item_Base(6287, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 30),
		Item_Base(6288, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 30),
		Item_Base(6289, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 30),
		Item_Base(6290, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 30),
		Item_Base(6291, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 30),
		Item_Base(6292, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 30),
		Item_Base(6293, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 30),
		Item_Base(6294, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 30),
		Item_Base(6295, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 30),
		Item_Base(6296, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 30),
		Item_Base(6297, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 30),
		Item_Base(6298, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 30),
		Item_Base(6299, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 30),
		Item_Base(6300, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 40),
		Item_Base(6301, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 40),
		Item_Base(6302, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 40),
		Item_Base(6303, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 40),
		Item_Base(6304, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 40),
		Item_Base(6305, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 40),
		Item_Base(6306, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 40),
		Item_Base(6307, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 40),
		Item_Base(6308, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 40),
		Item_Base(6309, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 40),
		Item_Base(6310, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 40),
		Item_Base(6311, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 40),
		Item_Base(6312, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 40),
		Item_Base(6313, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 40),
		Item_Base(6314, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 40),
		Item_Base(6315, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 40),
		Item_Base(6316, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 40),
		Item_Base(6317, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 40),
		Item_Base(6318, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 40),
		Item_Base(6319, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 40),
		Item_Base(6320, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 40),
		Item_Base(6321, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 40),
		Item_Base(6322, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 40),
		Item_Base(6323, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 40),
		Item_Base(6324, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 40),
		Item_Base(6325, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 50),
		Item_Base(6326, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 50),
		Item_Base(6327, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 50),
		Item_Base(6328, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 50),
		Item_Base(6329, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 50),
		Item_Base(6330, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 50),
		Item_Base(6331, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 50),
		Item_Base(6332, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 50),
		Item_Base(6333, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 50),
		Item_Base(6334, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 50),
		Item_Base(6335, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 50),
		Item_Base(6336, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 50),
		Item_Base(6337, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 50),
		Item_Base(6338, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 50),
		Item_Base(6339, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 50),
		Item_Base(6340, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 50),
		Item_Base(6341, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 50),
		Item_Base(6342, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 50),
		Item_Base(6343, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 50),
		Item_Base(6344, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 50),
		Item_Base(6345, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 50),
		Item_Base(6346, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 50),
		Item_Base(6347, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 50),
		Item_Base(6348, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 50),
		Item_Base(6349, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 50),
		Item_Base(6350, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 60),
		Item_Base(6351, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 60),
		Item_Base(6352, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 60),
		Item_Base(6353, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 60),
		Item_Base(6354, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 60),
		Item_Base(6355, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 60),
		Item_Base(6356, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 60),
		Item_Base(6357, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 60),
		Item_Base(6358, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 60),
		Item_Base(6359, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 60),
		Item_Base(6360, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 60),
		Item_Base(6361, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 60),
		Item_Base(6362, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 60),
		Item_Base(6363, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 60),
		Item_Base(6364, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 60),
		Item_Base(6365, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 60),
		Item_Base(6366, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 60),
		Item_Base(6367, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 60),
		Item_Base(6368, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 60),
		Item_Base(6369, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 60),
		Item_Base(6370, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 60),
		Item_Base(6371, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 60),
		Item_Base(6372, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 60),
		Item_Base(6373, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 60),
		Item_Base(6374, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 60),
		Item_Base(6375, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 70),
		Item_Base(6376, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 70),
		Item_Base(6377, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 70),
		Item_Base(6378, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 70),
		Item_Base(6379, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 70),
		Item_Base(6380, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 70),
		Item_Base(6381, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 70),
		Item_Base(6382, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 70),
		Item_Base(6383, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 70),
		Item_Base(6384, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 70),
		Item_Base(6385, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 70),
		Item_Base(6386, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 70),
		Item_Base(6387, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 70),
		Item_Base(6388, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 70),
		Item_Base(6389, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 70),
		Item_Base(6390, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 70),
		Item_Base(6391, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 70),
		Item_Base(6392, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 70),
		Item_Base(6393, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 70),
		Item_Base(6394, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 70),
		Item_Base(6395, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 70),
		Item_Base(6396, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 70),
		Item_Base(6397, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 70),
		Item_Base(6398, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 70),
		Item_Base(6399, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 70),
		Item_Base(6400, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 80),
		Item_Base(6401, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 80),
		Item_Base(6402, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 80),
		Item_Base(6403, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 80),
		Item_Base(6404, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 80),
		Item_Base(6405, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 80),
		Item_Base(6406, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 80),
		Item_Base(6407, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 80),
		Item_Base(6408, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 80),
		Item_Base(6409, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 80),
		Item_Base(6410, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 80),
		Item_Base(6411, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 80),
		Item_Base(6412, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 80),
		Item_Base(6413, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 80),
		Item_Base(6414, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 80),
		Item_Base(6415, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 80),
		Item_Base(6416, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 80),
		Item_Base(6417, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 80),
		Item_Base(6418, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 80),
		Item_Base(6419, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 80),
		Item_Base(6420, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 80),
		Item_Base(6421, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 80),
		Item_Base(6422, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 80),
		Item_Base(6423, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 80),
		Item_Base(6424, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 80),
		Item_Base(6425, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 90),
		Item_Base(6426, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 90),
		Item_Base(6427, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 90),
		Item_Base(6428, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 90),
		Item_Base(6429, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 90),
		Item_Base(6430, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 90),
		Item_Base(6431, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 90),
		Item_Base(6432, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 90),
		Item_Base(6433, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 90),
		Item_Base(6434, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 90),
		Item_Base(6435, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 90),
		Item_Base(6436, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 90),
		Item_Base(6437, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 90),
		Item_Base(6438, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 90),
		Item_Base(6439, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 90),
		Item_Base(6440, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 90),
		Item_Base(6441, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 90),
		Item_Base(6442, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 90),
		Item_Base(6443, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 90),
		Item_Base(6444, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 90),
		Item_Base(6445, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 90),
		Item_Base(6446, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 90),
		Item_Base(6447, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 90),
		Item_Base(6448, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 90),
		Item_Base(6449, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 90),
		Item_Base(6450, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 100),
		Item_Base(6451, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 100),
		Item_Base(6452, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 100),
		Item_Base(6453, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 100),
		Item_Base(6454, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 100),
		Item_Base(6455, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 100),
		Item_Base(6456, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 100),
		Item_Base(6457, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 100),
		Item_Base(6458, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 100),
		Item_Base(6459, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 100),
		Item_Base(6460, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 100),
		Item_Base(6461, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 100),
		Item_Base(6462, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 100),
		Item_Base(6463, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 100),
		Item_Base(6464, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 100),
		Item_Base(6465, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 100),
		Item_Base(6466, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 100),
		Item_Base(6467, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 100),
		Item_Base(6468, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 100),
		Item_Base(6469, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 100),
		Item_Base(6470, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 100),
		Item_Base(6471, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 100),
		Item_Base(6472, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 100),
		Item_Base(6473, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 100),
		Item_Base(6474, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 100),
		Item_Base(6475, LoC::Unique, LoC::Caster, LoC::SlotEar, 15360, 110),
		Item_Base(6476, LoC::Unique, LoC::Caster, LoC::SlotNeck, 15360, 110),
		Item_Base(6477, LoC::Unique, LoC::Caster, LoC::SlotFace, 15360, 110),
		Item_Base(6478, LoC::Unique, LoC::Caster, LoC::SlotHead, 15360, 110),
		Item_Base(6479, LoC::Unique, LoC::Caster, LoC::SlotFingers, 15360, 110),
		Item_Base(6480, LoC::Unique, LoC::Caster, LoC::SlotWrist, 15360, 110),
		Item_Base(6481, LoC::Unique, LoC::Caster, LoC::SlotArms, 15360, 110),
		Item_Base(6482, LoC::Unique, LoC::Caster, LoC::SlotHand, 15360, 110),
		Item_Base(6483, LoC::Unique, LoC::Caster, LoC::SlotShoulders, 15360, 110),
		Item_Base(6484, LoC::Unique, LoC::Caster, LoC::SlotChest, 15360, 110),
		Item_Base(6485, LoC::Unique, LoC::Caster, LoC::SlotBack, 15360, 110),
		Item_Base(6486, LoC::Unique, LoC::Caster, LoC::SlotWaist, 15360, 110),
		Item_Base(6487, LoC::Unique, LoC::Caster, LoC::SlotLegs, 15360, 110),
		Item_Base(6488, LoC::Unique, LoC::Caster, LoC::SlotFeet, 15360, 110),
		Item_Base(6489, LoC::Unique, LoC::Caster, LoC::SlotCharm, 15360, 110),
		Item_Base(6490, LoC::Unique, LoC::Caster, LoC::SlotPowerSource, 15360, 110),
		Item_Base(6491, LoC::Unique, LoC::Caster, LoC::SlotOneHB, 15360, 110),
		Item_Base(6492, LoC::Unique, LoC::Caster, LoC::SlotTwoHB, 15360, 110),
		Item_Base(6493, LoC::Unique, LoC::Caster, LoC::SlotOneHS, 15360, 110),
		Item_Base(6494, LoC::Unique, LoC::Caster, LoC::SlotTwoHS, 15360, 110),
		Item_Base(6495, LoC::Unique, LoC::Caster, LoC::SlotArchery, 15360, 110),
		Item_Base(6496, LoC::Unique, LoC::Caster, LoC::SlotThrowing, 15360, 110),
		Item_Base(6497, LoC::Unique, LoC::Caster, LoC::SlotOneHP, 15360, 110),
		Item_Base(6498, LoC::Unique, LoC::Caster, LoC::SlotTwoHP, 15360, 110),
		Item_Base(6499, LoC::Unique, LoC::Caster, LoC::SlotShield, 15360, 110),
		/* END OF AUTOGENERATED */
	};
	for (auto&& i : items) {
		if (i.rarity == rarity &&
			i.slot_type == slot_type &&
			i.class_type == class_type &&
			i.level == item_level) {
			return i.item_id;
		}
	}

	return 0;
}

//GetItemProperty is called during Itemization, and is autogenerated from the sheets
int NPC::GetItemProperty(int aug_index, int rarity, int slot_type, int class_type, int item_level) {
	if (aug_index <= 1) return 0;
	static Item_Property items[] = {
		/* AUTOGENERATED VIA SHEETS*/
		Item_Property(7001,1,3, 1, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7002,4,3, 1, 161, LoC::ClassAll, LoC::Common),
		Item_Property(7003,7,3, 1, 158, LoC::ClassAll, LoC::Common),
		Item_Property(7004,10,3, 1, 155, LoC::ClassAll, LoC::Common),
		Item_Property(7005,13,3, 1, 152, LoC::ClassAll, LoC::Common),
		Item_Property(7006,16,3, 1, 149, LoC::ClassAll, LoC::Common),
		Item_Property(7007,19,3, 1, 146, LoC::ClassAll, LoC::Common),
		Item_Property(7008,22,3, 1, 143, LoC::ClassAll, LoC::Common),
		Item_Property(7009,25,3, 1, 140, LoC::ClassAll, LoC::Common),
		Item_Property(7010,28,3, 1, 137, LoC::ClassAll, LoC::Common),
		Item_Property(7011,31,3, 1, 134, LoC::ClassAll, LoC::Common),
		Item_Property(7012,34,3, 1, 131, LoC::ClassAll, LoC::Common),
		Item_Property(7013,37,3, 1, 128, LoC::ClassAll, LoC::Common),
		Item_Property(7014,40,3, 1, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7015,43,3, 1, 122, LoC::ClassAll, LoC::Common),
		Item_Property(7016,46,3, 1, 119, LoC::ClassAll, LoC::Common),
		Item_Property(7017,49,3, 1, 116, LoC::ClassAll, LoC::Common),
		Item_Property(7018,52,3, 1, 113, LoC::ClassAll, LoC::Common),
		Item_Property(7019,55,3, 1, 110, LoC::ClassAll, LoC::Common),
		Item_Property(7020,58,3, 1, 107, LoC::ClassAll, LoC::Common),
		Item_Property(7021,61,3, 1, 104, LoC::ClassAll, LoC::Common),
		Item_Property(7022,64,3, 1, 101, LoC::ClassAll, LoC::Common),
		Item_Property(7023,67,3, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7024,70,3, 1, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7025,73,3, 1, 92, LoC::ClassAll, LoC::Common),
		Item_Property(7026,76,3, 1, 89, LoC::ClassAll, LoC::Common),
		Item_Property(7027,79,3, 1, 86, LoC::ClassAll, LoC::Common),
		Item_Property(7028,82,3, 1, 83, LoC::ClassAll, LoC::Common),
		Item_Property(7029,85,3, 1, 80, LoC::ClassAll, LoC::Common),
		Item_Property(7030,88,3, 1, 77, LoC::ClassAll, LoC::Common),
		Item_Property(7031,91,3, 1, 74, LoC::ClassAll, LoC::Common),
		Item_Property(7032,94,3, 1, 71, LoC::ClassAll, LoC::Common),
		Item_Property(7033,97,3, 1, 68, LoC::ClassAll, LoC::Common),
		Item_Property(7034,100,3, 1, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7035,103,3, 1, 62, LoC::ClassAll, LoC::Common),
		Item_Property(7036,106,3, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7037,109,3, 1, 56, LoC::ClassAll, LoC::Common),
		Item_Property(7038,112,3, 1, 53, LoC::ClassAll, LoC::Common),
		Item_Property(7039,115,3, 1, 50, LoC::ClassAll, LoC::Common),
		Item_Property(7040,118,3, 1, 47, LoC::ClassAll, LoC::Common),
		Item_Property(7041,1,3, 1, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7042,4,3, 1, 161, LoC::ClassAll, LoC::Common),
		Item_Property(7043,7,3, 1, 158, LoC::ClassAll, LoC::Common),
		Item_Property(7044,10,3, 1, 155, LoC::ClassAll, LoC::Common),
		Item_Property(7045,13,3, 1, 152, LoC::ClassAll, LoC::Common),
		Item_Property(7046,16,3, 1, 149, LoC::ClassAll, LoC::Common),
		Item_Property(7047,19,3, 1, 146, LoC::ClassAll, LoC::Common),
		Item_Property(7048,22,3, 1, 143, LoC::ClassAll, LoC::Common),
		Item_Property(7049,25,3, 1, 140, LoC::ClassAll, LoC::Common),
		Item_Property(7050,28,3, 1, 137, LoC::ClassAll, LoC::Common),
		Item_Property(7051,31,3, 1, 134, LoC::ClassAll, LoC::Common),
		Item_Property(7052,34,3, 1, 131, LoC::ClassAll, LoC::Common),
		Item_Property(7053,37,3, 1, 128, LoC::ClassAll, LoC::Common),
		Item_Property(7054,40,3, 1, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7055,43,3, 1, 122, LoC::ClassAll, LoC::Common),
		Item_Property(7056,46,3, 1, 119, LoC::ClassAll, LoC::Common),
		Item_Property(7057,49,3, 1, 116, LoC::ClassAll, LoC::Common),
		Item_Property(7058,52,3, 1, 113, LoC::ClassAll, LoC::Common),
		Item_Property(7059,55,3, 1, 110, LoC::ClassAll, LoC::Common),
		Item_Property(7060,58,3, 1, 107, LoC::ClassAll, LoC::Common),
		Item_Property(7061,61,3, 1, 104, LoC::ClassAll, LoC::Common),
		Item_Property(7062,64,3, 1, 101, LoC::ClassAll, LoC::Common),
		Item_Property(7063,67,3, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7064,70,3, 1, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7065,73,3, 1, 92, LoC::ClassAll, LoC::Common),
		Item_Property(7066,76,3, 1, 89, LoC::ClassAll, LoC::Common),
		Item_Property(7067,79,3, 1, 86, LoC::ClassAll, LoC::Common),
		Item_Property(7068,82,3, 1, 83, LoC::ClassAll, LoC::Common),
		Item_Property(7069,85,3, 1, 80, LoC::ClassAll, LoC::Common),
		Item_Property(7070,88,3, 1, 77, LoC::ClassAll, LoC::Common),
		Item_Property(7071,91,3, 1, 74, LoC::ClassAll, LoC::Common),
		Item_Property(7072,94,3, 1, 71, LoC::ClassAll, LoC::Common),
		Item_Property(7073,97,3, 1, 68, LoC::ClassAll, LoC::Common),
		Item_Property(7074,100,3, 1, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7075,103,3, 1, 62, LoC::ClassAll, LoC::Common),
		Item_Property(7076,106,3, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7077,109,3, 1, 56, LoC::ClassAll, LoC::Common),
		Item_Property(7078,112,3, 1, 53, LoC::ClassAll, LoC::Common),
		Item_Property(7079,115,3, 1, 50, LoC::ClassAll, LoC::Common),
		Item_Property(7080,118,3, 1, 47, LoC::ClassAll, LoC::Common),
		Item_Property(7081,1,3, 1, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7082,4,3, 1, 161, LoC::ClassAll, LoC::Common),
		Item_Property(7083,7,3, 1, 158, LoC::ClassAll, LoC::Common),
		Item_Property(7084,10,3, 1, 155, LoC::ClassAll, LoC::Common),
		Item_Property(7085,13,3, 1, 152, LoC::ClassAll, LoC::Common),
		Item_Property(7086,16,3, 1, 149, LoC::ClassAll, LoC::Common),
		Item_Property(7087,19,3, 1, 146, LoC::ClassAll, LoC::Common),
		Item_Property(7088,22,3, 1, 143, LoC::ClassAll, LoC::Common),
		Item_Property(7089,25,3, 1, 140, LoC::ClassAll, LoC::Common),
		Item_Property(7090,28,3, 1, 137, LoC::ClassAll, LoC::Common),
		Item_Property(7091,31,3, 1, 134, LoC::ClassAll, LoC::Common),
		Item_Property(7092,34,3, 1, 131, LoC::ClassAll, LoC::Common),
		Item_Property(7093,37,3, 1, 128, LoC::ClassAll, LoC::Common),
		Item_Property(7094,40,3, 1, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7095,43,3, 1, 122, LoC::ClassAll, LoC::Common),
		Item_Property(7096,46,3, 1, 119, LoC::ClassAll, LoC::Common),
		Item_Property(7097,49,3, 1, 116, LoC::ClassAll, LoC::Common),
		Item_Property(7098,52,3, 1, 113, LoC::ClassAll, LoC::Common),
		Item_Property(7099,55,3, 1, 110, LoC::ClassAll, LoC::Common),
		Item_Property(7100,58,3, 1, 107, LoC::ClassAll, LoC::Common),
		Item_Property(7101,61,3, 1, 104, LoC::ClassAll, LoC::Common),
		Item_Property(7102,64,3, 1, 101, LoC::ClassAll, LoC::Common),
		Item_Property(7103,67,3, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7104,70,3, 1, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7105,73,3, 1, 92, LoC::ClassAll, LoC::Common),
		Item_Property(7106,76,3, 1, 89, LoC::ClassAll, LoC::Common),
		Item_Property(7107,79,3, 1, 86, LoC::ClassAll, LoC::Common),
		Item_Property(7108,82,3, 1, 83, LoC::ClassAll, LoC::Common),
		Item_Property(7109,85,3, 1, 80, LoC::ClassAll, LoC::Common),
		Item_Property(7110,88,3, 1, 77, LoC::ClassAll, LoC::Common),
		Item_Property(7111,91,3, 1, 74, LoC::ClassAll, LoC::Common),
		Item_Property(7112,94,3, 1, 71, LoC::ClassAll, LoC::Common),
		Item_Property(7113,97,3, 1, 68, LoC::ClassAll, LoC::Common),
		Item_Property(7114,100,3, 1, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7115,103,3, 1, 62, LoC::ClassAll, LoC::Common),
		Item_Property(7116,106,3, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7117,109,3, 1, 56, LoC::ClassAll, LoC::Common),
		Item_Property(7118,112,3, 1, 53, LoC::ClassAll, LoC::Common),
		Item_Property(7119,115,3, 1, 50, LoC::ClassAll, LoC::Common),
		Item_Property(7120,118,3, 1, 47, LoC::ClassAll, LoC::Common),
		Item_Property(7121,10,2, 1, 77.5, LoC::ClassAll, LoC::Common),
		Item_Property(7122,20,2, 1, 72.5, LoC::ClassAll, LoC::Common),
		Item_Property(7123,30,2, 1, 67.5, LoC::ClassAll, LoC::Common),
		Item_Property(7124,40,2, 1, 62.5, LoC::ClassAll, LoC::Common),
		Item_Property(7125,50,2, 1, 57.5, LoC::ClassAll, LoC::Common),
		Item_Property(7126,60,2, 1, 52.5, LoC::ClassAll, LoC::Common),
		Item_Property(7127,70,2, 1, 47.5, LoC::ClassAll, LoC::Common),
		Item_Property(7128,80,2, 1, 42.5, LoC::ClassAll, LoC::Common),
		Item_Property(7129,90,2, 1, 37.5, LoC::ClassAll, LoC::Common),
		Item_Property(7130,100,2, 1, 32.5, LoC::ClassAll, LoC::Common),
		Item_Property(7131,1,2, 1, 82, LoC::ClassAll, LoC::Common),
		Item_Property(7132,15,2, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7133,30,2, 1, 67.5, LoC::ClassAll, LoC::Common),
		Item_Property(7134,40,2, 1, 62.5, LoC::ClassAll, LoC::Common),
		Item_Property(7135,50,2, 1, 57.5, LoC::ClassAll, LoC::Common),
		Item_Property(7136,60,2, 1, 52.5, LoC::ClassAll, LoC::Common),
		Item_Property(7137,70,2, 1, 47.5, LoC::ClassAll, LoC::Common),
		Item_Property(7138,80,2, 1, 42.5, LoC::ClassAll, LoC::Common),
		Item_Property(7139,90,2, 1, 37.5, LoC::ClassAll, LoC::Common),
		Item_Property(7140,100,2, 1, 32.5, LoC::ClassAll, LoC::Common),
		Item_Property(7141,1,2, 1, 82, LoC::ClassAll, LoC::Common),
		Item_Property(7142,15,2, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7143,30,2, 1, 67.5, LoC::ClassAll, LoC::Common),
		Item_Property(7144,40,2, 1, 62.5, LoC::ClassAll, LoC::Common),
		Item_Property(7145,50,2, 1, 57.5, LoC::ClassAll, LoC::Common),
		Item_Property(7146,60,2, 1, 52.5, LoC::ClassAll, LoC::Common),
		Item_Property(7147,70,2, 1, 47.5, LoC::ClassAll, LoC::Common),
		Item_Property(7148,80,2, 1, 42.5, LoC::ClassAll, LoC::Common),
		Item_Property(7149,90,2, 1, 37.5, LoC::ClassAll, LoC::Common),
		Item_Property(7150,100,2, 1, 32.5, LoC::ClassAll, LoC::Common),
		Item_Property(7151,1,2, 1, 82, LoC::ClassAll, LoC::Common),
		Item_Property(7152,15,2, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7153,30,2, 1, 67.5, LoC::ClassAll, LoC::Common),
		Item_Property(7154,40,2, 1, 62.5, LoC::ClassAll, LoC::Common),
		Item_Property(7155,50,2, 1, 57.5, LoC::ClassAll, LoC::Common),
		Item_Property(7156,60,2, 1, 52.5, LoC::ClassAll, LoC::Common),
		Item_Property(7157,70,2, 1, 47.5, LoC::ClassAll, LoC::Common),
		Item_Property(7158,80,2, 1, 42.5, LoC::ClassAll, LoC::Common),
		Item_Property(7159,90,2, 1, 37.5, LoC::ClassAll, LoC::Common),
		Item_Property(7160,100,2, 1, 32.5, LoC::ClassAll, LoC::Common),
		Item_Property(7161,1,2, 1, 82, LoC::ClassAll, LoC::Common),
		Item_Property(7162,15,2, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7163,30,2, 1, 67.5, LoC::ClassAll, LoC::Common),
		Item_Property(7164,40,2, 1, 62.5, LoC::ClassAll, LoC::Common),
		Item_Property(7165,50,2, 1, 57.5, LoC::ClassAll, LoC::Common),
		Item_Property(7166,60,2, 1, 52.5, LoC::ClassAll, LoC::Common),
		Item_Property(7167,70,2, 1, 47.5, LoC::ClassAll, LoC::Common),
		Item_Property(7168,80,2, 1, 42.5, LoC::ClassAll, LoC::Common),
		Item_Property(7169,90,2, 1, 37.5, LoC::ClassAll, LoC::Common),
		Item_Property(7170,100,2, 1, 32.5, LoC::ClassAll, LoC::Common),
		Item_Property(7171,1,2, 1, 82, LoC::ClassAll, LoC::Common),
		Item_Property(7172,15,2, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7173,30,2, 1, 67.5, LoC::ClassAll, LoC::Common),
		Item_Property(7174,40,2, 1, 62.5, LoC::ClassAll, LoC::Common),
		Item_Property(7175,50,2, 1, 57.5, LoC::ClassAll, LoC::Common),
		Item_Property(7176,60,2, 1, 52.5, LoC::ClassAll, LoC::Common),
		Item_Property(7177,70,2, 1, 47.5, LoC::ClassAll, LoC::Common),
		Item_Property(7178,80,2, 1, 42.5, LoC::ClassAll, LoC::Common),
		Item_Property(7179,90,2, 1, 37.5, LoC::ClassAll, LoC::Common),
		Item_Property(7180,100,2, 1, 32.5, LoC::ClassAll, LoC::Common),
		Item_Property(7181,1,2, 1, 82, LoC::ClassAll, LoC::Common),
		Item_Property(7182,15,2, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7183,30,2, 1, 67.5, LoC::ClassAll, LoC::Common),
		Item_Property(7184,40,2, 1, 62.5, LoC::ClassAll, LoC::Common),
		Item_Property(7185,50,2, 1, 57.5, LoC::ClassAll, LoC::Common),
		Item_Property(7186,60,2, 1, 52.5, LoC::ClassAll, LoC::Common),
		Item_Property(7187,70,2, 1, 47.5, LoC::ClassAll, LoC::Common),
		Item_Property(7188,80,2, 1, 42.5, LoC::ClassAll, LoC::Common),
		Item_Property(7189,90,2, 1, 37.5, LoC::ClassAll, LoC::Common),
		Item_Property(7190,100,2, 1, 32.5, LoC::ClassAll, LoC::Common),
		Item_Property(7191,1,4, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7192,15,4, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7193,30,4, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7194,40,4, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7195,50,4, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7196,60,4, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7197,70,4, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7198,80,4, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7199,90,4, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7200,100,4, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7201,1,4, 1, 464, LoC::ClassAll, LoC::Common),
		Item_Property(7202,15,4, 1, 450, LoC::ClassAll, LoC::Common),
		Item_Property(7203,30,4, 1, 435, LoC::ClassAll, LoC::Common),
		Item_Property(7204,40,4, 1, 425, LoC::ClassAll, LoC::Common),
		Item_Property(7205,50,4, 1, 415, LoC::ClassAll, LoC::Common),
		Item_Property(7206,60,4, 1, 405, LoC::ClassAll, LoC::Common),
		Item_Property(7207,70,4, 1, 395, LoC::ClassAll, LoC::Common),
		Item_Property(7208,80,4, 1, 385, LoC::ClassAll, LoC::Common),
		Item_Property(7209,90,4, 1, 375, LoC::ClassAll, LoC::Common),
		Item_Property(7210,100,4, 1, 365, LoC::ClassAll, LoC::Common),
		Item_Property(7211,1,4, 1, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7212,15,4, 1, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7213,30,4, 1, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7214,40,4, 1, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7215,50,4, 1, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7216,60,4, 1, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7217,70,4, 1, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7218,80,4, 1, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7219,90,4, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7220,100,4, 1, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7221,1,4, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7222,15,4, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7223,30,4, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7224,40,4, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7225,50,4, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7226,60,4, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7227,70,4, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7228,80,4, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7229,90,4, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7230,100,4, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7231,1,4, 7, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7232,15,4, 7, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7233,30,4, 7, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7234,40,4, 7, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7235,50,4, 7, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7236,60,4, 7, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7237,70,4, 7, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7238,80,4, 7, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7239,90,4, 7, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7240,100,4, 7, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7241,1,4, 1, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7242,15,4, 1, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7243,30,4, 1, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7244,40,4, 1, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7245,50,4, 1, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7246,60,4, 1, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7247,70,4, 1, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7248,80,4, 1, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7249,90,4, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7250,100,4, 1, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7251,1,4, 7, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7252,15,4, 7, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7253,30,4, 7, 270, LoC::ClassAll, LoC::Common),
		Item_Property(7254,40,4, 7, 250, LoC::ClassAll, LoC::Common),
		Item_Property(7255,50,4, 7, 230, LoC::ClassAll, LoC::Common),
		Item_Property(7256,60,4, 7, 210, LoC::ClassAll, LoC::Common),
		Item_Property(7257,70,4, 7, 190, LoC::ClassAll, LoC::Common),
		Item_Property(7258,80,4, 7, 170, LoC::ClassAll, LoC::Common),
		Item_Property(7259,90,4, 7, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7260,100,4, 7, 130, LoC::ClassAll, LoC::Common),
		Item_Property(7261,1,3, 3, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7262,15,3, 3, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7263,30,3, 3, 270, LoC::ClassAll, LoC::Common),
		Item_Property(7264,40,3, 3, 250, LoC::ClassAll, LoC::Common),
		Item_Property(7265,50,3, 3, 230, LoC::ClassAll, LoC::Common),
		Item_Property(7266,60,3, 3, 210, LoC::ClassAll, LoC::Common),
		Item_Property(7267,70,3, 3, 190, LoC::ClassAll, LoC::Common),
		Item_Property(7268,80,3, 3, 170, LoC::ClassAll, LoC::Common),
		Item_Property(7269,90,3, 3, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7270,100,3, 3, 130, LoC::ClassAll, LoC::Common),
		Item_Property(7271,1,3, 3, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7272,15,3, 3, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7273,30,3, 3, 270, LoC::ClassAll, LoC::Common),
		Item_Property(7274,40,3, 3, 250, LoC::ClassAll, LoC::Common),
		Item_Property(7275,50,3, 3, 230, LoC::ClassAll, LoC::Common),
		Item_Property(7276,60,3, 3, 210, LoC::ClassAll, LoC::Common),
		Item_Property(7277,70,3, 3, 190, LoC::ClassAll, LoC::Common),
		Item_Property(7278,80,3, 3, 170, LoC::ClassAll, LoC::Common),
		Item_Property(7279,90,3, 3, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7280,100,3, 3, 130, LoC::ClassAll, LoC::Common),
		Item_Property(7281,1,3, 3, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7282,15,3, 3, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7283,30,3, 3, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7284,40,3, 3, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7285,50,3, 3, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7286,60,3, 3, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7287,70,3, 3, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7288,80,3, 3, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7289,90,3, 3, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7290,100,3, 3, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7291,1,4, 1, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7292,15,4, 1, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7293,30,4, 1, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7294,40,4, 1, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7295,50,4, 1, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7296,60,4, 1, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7297,70,4, 1, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7298,80,4, 1, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7299,90,4, 1, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7300,100,4, 1, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7301,1,4, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7302,15,4, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7303,30,4, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7304,40,4, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7305,50,4, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7306,60,4, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7307,70,4, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7308,80,4, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7309,90,4, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7310,100,4, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7311,1,4, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7312,15,4, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7313,30,4, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7314,40,4, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7315,50,4, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7316,60,4, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7317,70,4, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7318,80,4, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7319,90,4, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7320,100,4, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7321,1,2, 6, 656, LoC::ClassAll, LoC::Common),
		Item_Property(7322,2,2, 6, 652, LoC::ClassAll, LoC::Common),
		Item_Property(7323,3,2, 6, 648, LoC::ClassAll, LoC::Common),
		Item_Property(7324,4,2, 6, 644, LoC::ClassAll, LoC::Common),
		Item_Property(7325,5,2, 6, 640, LoC::ClassAll, LoC::Common),
		Item_Property(7326,6,2, 6, 636, LoC::ClassAll, LoC::Common),
		Item_Property(7327,7,2, 6, 632, LoC::ClassAll, LoC::Common),
		Item_Property(7328,8,2, 6, 628, LoC::ClassAll, LoC::Common),
		Item_Property(7329,9,2, 6, 624, LoC::ClassAll, LoC::Common),
		Item_Property(7330,10,2, 6, 620, LoC::ClassAll, LoC::Common),
		Item_Property(7331,11,2, 6, 616, LoC::ClassAll, LoC::Common),
		Item_Property(7332,12,2, 6, 612, LoC::ClassAll, LoC::Common),
		Item_Property(7333,13,2, 6, 608, LoC::ClassAll, LoC::Common),
		Item_Property(7334,14,2, 6, 604, LoC::ClassAll, LoC::Common),
		Item_Property(7335,15,2, 6, 600, LoC::ClassAll, LoC::Common),
		Item_Property(7336,16,2, 6, 596, LoC::ClassAll, LoC::Common),
		Item_Property(7337,17,2, 6, 592, LoC::ClassAll, LoC::Common),
		Item_Property(7338,18,2, 6, 588, LoC::ClassAll, LoC::Common),
		Item_Property(7339,19,2, 6, 584, LoC::ClassAll, LoC::Common),
		Item_Property(7340,20,2, 6, 580, LoC::ClassAll, LoC::Common),
		Item_Property(7341,21,2, 6, 576, LoC::ClassAll, LoC::Common),
		Item_Property(7342,22,2, 6, 572, LoC::ClassAll, LoC::Common),
		Item_Property(7343,23,2, 6, 568, LoC::ClassAll, LoC::Common),
		Item_Property(7344,24,2, 6, 564, LoC::ClassAll, LoC::Common),
		Item_Property(7345,25,2, 6, 560, LoC::ClassAll, LoC::Common),
		Item_Property(7346,26,2, 6, 556, LoC::ClassAll, LoC::Common),
		Item_Property(7347,27,2, 6, 552, LoC::ClassAll, LoC::Common),
		Item_Property(7348,28,2, 6, 548, LoC::ClassAll, LoC::Common),
		Item_Property(7349,29,2, 6, 544, LoC::ClassAll, LoC::Common),
		Item_Property(7350,30,2, 6, 540, LoC::ClassAll, LoC::Common),
		Item_Property(7351,31,2, 6, 536, LoC::ClassAll, LoC::Common),
		Item_Property(7352,32,2, 6, 532, LoC::ClassAll, LoC::Common),
		Item_Property(7353,33,2, 6, 528, LoC::ClassAll, LoC::Common),
		Item_Property(7354,34,2, 6, 524, LoC::ClassAll, LoC::Common),
		Item_Property(7355,35,2, 6, 520, LoC::ClassAll, LoC::Common),
		Item_Property(7356,36,2, 6, 516, LoC::ClassAll, LoC::Common),
		Item_Property(7357,37,2, 6, 512, LoC::ClassAll, LoC::Common),
		Item_Property(7358,38,2, 6, 508, LoC::ClassAll, LoC::Common),
		Item_Property(7359,39,2, 6, 504, LoC::ClassAll, LoC::Common),
		Item_Property(7360,40,2, 6, 500, LoC::ClassAll, LoC::Common),
		Item_Property(7361,41,2, 6, 496, LoC::ClassAll, LoC::Common),
		Item_Property(7362,42,2, 6, 492, LoC::ClassAll, LoC::Common),
		Item_Property(7363,43,2, 6, 488, LoC::ClassAll, LoC::Common),
		Item_Property(7364,44,2, 6, 484, LoC::ClassAll, LoC::Common),
		Item_Property(7365,45,2, 6, 480, LoC::ClassAll, LoC::Common),
		Item_Property(7366,46,2, 6, 476, LoC::ClassAll, LoC::Common),
		Item_Property(7367,47,2, 6, 472, LoC::ClassAll, LoC::Common),
		Item_Property(7368,48,2, 6, 468, LoC::ClassAll, LoC::Common),
		Item_Property(7369,49,2, 6, 464, LoC::ClassAll, LoC::Common),
		Item_Property(7370,50,2, 6, 460, LoC::ClassAll, LoC::Common),
		Item_Property(7371,51,2, 6, 456, LoC::ClassAll, LoC::Common),
		Item_Property(7372,52,2, 6, 452, LoC::ClassAll, LoC::Common),
		Item_Property(7373,53,2, 6, 448, LoC::ClassAll, LoC::Common),
		Item_Property(7374,54,2, 6, 444, LoC::ClassAll, LoC::Common),
		Item_Property(7375,55,2, 6, 440, LoC::ClassAll, LoC::Common),
		Item_Property(7376,56,2, 6, 436, LoC::ClassAll, LoC::Common),
		Item_Property(7377,57,2, 6, 432, LoC::ClassAll, LoC::Common),
		Item_Property(7378,58,2, 6, 428, LoC::ClassAll, LoC::Common),
		Item_Property(7379,59,2, 6, 424, LoC::ClassAll, LoC::Common),
		Item_Property(7380,60,2, 6, 420, LoC::ClassAll, LoC::Common),
		Item_Property(7381,61,2, 6, 416, LoC::ClassAll, LoC::Common),
		Item_Property(7382,62,2, 6, 412, LoC::ClassAll, LoC::Common),
		Item_Property(7383,63,2, 6, 408, LoC::ClassAll, LoC::Common),
		Item_Property(7384,64,2, 6, 404, LoC::ClassAll, LoC::Common),
		Item_Property(7385,65,2, 6, 400, LoC::ClassAll, LoC::Common),
		Item_Property(7386,66,2, 6, 396, LoC::ClassAll, LoC::Common),
		Item_Property(7387,67,2, 6, 392, LoC::ClassAll, LoC::Common),
		Item_Property(7388,68,2, 6, 388, LoC::ClassAll, LoC::Common),
		Item_Property(7389,69,2, 6, 384, LoC::ClassAll, LoC::Common),
		Item_Property(7390,70,2, 6, 380, LoC::ClassAll, LoC::Common),
		Item_Property(7391,71,2, 6, 376, LoC::ClassAll, LoC::Common),
		Item_Property(7392,72,2, 6, 372, LoC::ClassAll, LoC::Common),
		Item_Property(7393,73,2, 6, 368, LoC::ClassAll, LoC::Common),
		Item_Property(7394,74,2, 6, 364, LoC::ClassAll, LoC::Common),
		Item_Property(7395,75,2, 6, 360, LoC::ClassAll, LoC::Common),
		Item_Property(7396,76,2, 6, 356, LoC::ClassAll, LoC::Common),
		Item_Property(7397,77,2, 6, 352, LoC::ClassAll, LoC::Common),
		Item_Property(7398,78,2, 6, 348, LoC::ClassAll, LoC::Common),
		Item_Property(7399,79,2, 6, 344, LoC::ClassAll, LoC::Common),
		Item_Property(7400,80,2, 6, 340, LoC::ClassAll, LoC::Common),
		Item_Property(7401,81,2, 6, 336, LoC::ClassAll, LoC::Common),
		Item_Property(7402,82,2, 6, 332, LoC::ClassAll, LoC::Common),
		Item_Property(7403,83,2, 6, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7404,84,2, 6, 324, LoC::ClassAll, LoC::Common),
		Item_Property(7405,85,2, 6, 320, LoC::ClassAll, LoC::Common),
		Item_Property(7406,86,2, 6, 316, LoC::ClassAll, LoC::Common),
		Item_Property(7407,87,2, 6, 312, LoC::ClassAll, LoC::Common),
		Item_Property(7408,88,2, 6, 308, LoC::ClassAll, LoC::Common),
		Item_Property(7409,89,2, 6, 304, LoC::ClassAll, LoC::Common),
		Item_Property(7410,90,2, 6, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7411,91,2, 6, 296, LoC::ClassAll, LoC::Common),
		Item_Property(7412,92,2, 6, 292, LoC::ClassAll, LoC::Common),
		Item_Property(7413,93,2, 6, 288, LoC::ClassAll, LoC::Common),
		Item_Property(7414,94,2, 6, 284, LoC::ClassAll, LoC::Common),
		Item_Property(7415,95,2, 6, 280, LoC::ClassAll, LoC::Common),
		Item_Property(7416,96,2, 6, 276, LoC::ClassAll, LoC::Common),
		Item_Property(7417,97,2, 6, 272, LoC::ClassAll, LoC::Common),
		Item_Property(7418,98,2, 6, 268, LoC::ClassAll, LoC::Common),
		Item_Property(7419,99,2, 6, 264, LoC::ClassAll, LoC::Common),
		Item_Property(7420,100,2, 6, 260, LoC::ClassAll, LoC::Common),
		Item_Property(7421,101,2, 6, 256, LoC::ClassAll, LoC::Common),
		Item_Property(7422,102,2, 6, 252, LoC::ClassAll, LoC::Common),
		Item_Property(7423,103,2, 6, 248, LoC::ClassAll, LoC::Common),
		Item_Property(7424,104,2, 6, 244, LoC::ClassAll, LoC::Common),
		Item_Property(7425,105,2, 6, 240, LoC::ClassAll, LoC::Common),
		Item_Property(7426,106,2, 6, 236, LoC::ClassAll, LoC::Common),
		Item_Property(7427,107,2, 6, 232, LoC::ClassAll, LoC::Common),
		Item_Property(7428,108,2, 6, 228, LoC::ClassAll, LoC::Common),
		Item_Property(7429,109,2, 6, 224, LoC::ClassAll, LoC::Common),
		Item_Property(7430,110,2, 6, 220, LoC::ClassAll, LoC::Common),
		Item_Property(7431,111,2, 6, 216, LoC::ClassAll, LoC::Common),
		Item_Property(7432,112,2, 6, 212, LoC::ClassAll, LoC::Common),
		Item_Property(7433,113,2, 6, 208, LoC::ClassAll, LoC::Common),
		Item_Property(7434,114,2, 6, 204, LoC::ClassAll, LoC::Common),
		Item_Property(7435,115,2, 6, 200, LoC::ClassAll, LoC::Common),
		Item_Property(7436,116,2, 6, 196, LoC::ClassAll, LoC::Common),
		Item_Property(7437,117,2, 6, 192, LoC::ClassAll, LoC::Common),
		Item_Property(7438,118,2, 6, 188, LoC::ClassAll, LoC::Common),
		Item_Property(7439,119,2, 6, 184, LoC::ClassAll, LoC::Common),
		Item_Property(7440,120,2, 6, 180, LoC::ClassAll, LoC::Common),
		Item_Property(7441,121,2, 6, 176, LoC::ClassAll, LoC::Common),
		Item_Property(7442,122,2, 6, 172, LoC::ClassAll, LoC::Common),
		Item_Property(7443,123,2, 6, 168, LoC::ClassAll, LoC::Common),
		Item_Property(7444,124,2, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7445,125,2, 6, 160, LoC::ClassAll, LoC::Common),
		Item_Property(7446,126,2, 6, 156, LoC::ClassAll, LoC::Common),
		Item_Property(7447,127,2, 6, 152, LoC::ClassAll, LoC::Common),
		Item_Property(7448,128,2, 6, 148, LoC::ClassAll, LoC::Common),
		Item_Property(7449,129,2, 6, 144, LoC::ClassAll, LoC::Common),
		Item_Property(7450,130,2, 6, 140, LoC::ClassAll, LoC::Common),
		Item_Property(7451,131,2, 6, 136, LoC::ClassAll, LoC::Common),
		Item_Property(7452,132,2, 6, 132, LoC::ClassAll, LoC::Common),
		Item_Property(7453,133,2, 6, 128, LoC::ClassAll, LoC::Common),
		Item_Property(7454,134,2, 6, 124, LoC::ClassAll, LoC::Common),
		Item_Property(7455,135,2, 6, 120, LoC::ClassAll, LoC::Common),
		Item_Property(7456,136,2, 6, 116, LoC::ClassAll, LoC::Common),
		Item_Property(7457,137,2, 6, 112, LoC::ClassAll, LoC::Common),
		Item_Property(7458,138,2, 6, 108, LoC::ClassAll, LoC::Common),
		Item_Property(7459,139,2, 6, 104, LoC::ClassAll, LoC::Common),
		Item_Property(7460,140,2, 6, 100, LoC::ClassAll, LoC::Common),
		Item_Property(7461,141,2, 6, 96, LoC::ClassAll, LoC::Common),
		Item_Property(7462,142,2, 6, 92, LoC::ClassAll, LoC::Common),
		Item_Property(7463,143,2, 6, 88, LoC::ClassAll, LoC::Common),
		Item_Property(7464,144,2, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7465,145,2, 6, 80, LoC::ClassAll, LoC::Common),
		Item_Property(7466,146,2, 6, 76, LoC::ClassAll, LoC::Common),
		Item_Property(7467,147,2, 6, 72, LoC::ClassAll, LoC::Common),
		Item_Property(7468,148,2, 6, 68, LoC::ClassAll, LoC::Common),
		Item_Property(7469,149,2, 6, 64, LoC::ClassAll, LoC::Common),
		Item_Property(7470,150,2, 6, 60, LoC::ClassAll, LoC::Common),
		Item_Property(7471,1,4, 2, 98, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7472,15,4, 2, 84, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7473,30,4, 2, 69, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7474,40,4, 2, 59, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7475,50,4, 2, 49, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7476,60,4, 2, 39, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7477,70,4, 2, 29, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7478,80,4, 2, 19, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7479,90,4, 2, 9, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7480,100,4, 2, 1, LoC::ClassAll, LoC::Uncommon),
		Item_Property(7481,1,4, 8, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7482,15,4, 8, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7483,30,4, 8, 270, LoC::ClassAll, LoC::Common),
		Item_Property(7484,40,4, 8, 250, LoC::ClassAll, LoC::Common),
		Item_Property(7485,50,4, 8, 230, LoC::ClassAll, LoC::Common),
		Item_Property(7486,60,4, 8, 210, LoC::ClassAll, LoC::Common),
		Item_Property(7487,70,4, 8, 190, LoC::ClassAll, LoC::Common),
		Item_Property(7488,80,4, 8, 170, LoC::ClassAll, LoC::Common),
		Item_Property(7489,90,4, 8, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7490,100,4, 8, 130, LoC::ClassAll, LoC::Common),
		Item_Property(7491,1,4, 8, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7492,15,4, 8, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7493,30,4, 8, 270, LoC::ClassAll, LoC::Common),
		Item_Property(7494,40,4, 8, 250, LoC::ClassAll, LoC::Common),
		Item_Property(7495,50,4, 8, 230, LoC::ClassAll, LoC::Common),
		Item_Property(7496,60,4, 8, 210, LoC::ClassAll, LoC::Common),
		Item_Property(7497,70,4, 8, 190, LoC::ClassAll, LoC::Common),
		Item_Property(7498,80,4, 8, 170, LoC::ClassAll, LoC::Common),
		Item_Property(7499,90,4, 8, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7500,100,4, 8, 130, LoC::ClassAll, LoC::Common),
		Item_Property(7501,1,4, 8, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7502,15,4, 8, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7503,30,4, 8, 270, LoC::ClassAll, LoC::Common),
		Item_Property(7504,40,4, 8, 250, LoC::ClassAll, LoC::Common),
		Item_Property(7505,50,4, 8, 230, LoC::ClassAll, LoC::Common),
		Item_Property(7506,60,4, 8, 210, LoC::ClassAll, LoC::Common),
		Item_Property(7507,70,4, 8, 190, LoC::ClassAll, LoC::Common),
		Item_Property(7508,80,4, 8, 170, LoC::ClassAll, LoC::Common),
		Item_Property(7509,90,4, 8, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7510,100,4, 8, 130, LoC::ClassAll, LoC::Common),
		Item_Property(7511,1,4, 8, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7512,15,4, 8, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7513,30,4, 8, 270, LoC::ClassAll, LoC::Common),
		Item_Property(7514,40,4, 8, 250, LoC::ClassAll, LoC::Common),
		Item_Property(7515,50,4, 8, 230, LoC::ClassAll, LoC::Common),
		Item_Property(7516,60,4, 8, 210, LoC::ClassAll, LoC::Common),
		Item_Property(7517,70,4, 8, 190, LoC::ClassAll, LoC::Common),
		Item_Property(7518,80,4, 8, 170, LoC::ClassAll, LoC::Common),
		Item_Property(7519,90,4, 8, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7520,100,4, 8, 130, LoC::ClassAll, LoC::Common),
		Item_Property(7521,1,4, 8, 328, LoC::ClassAll, LoC::Common),
		Item_Property(7522,15,4, 8, 300, LoC::ClassAll, LoC::Common),
		Item_Property(7523,30,4, 8, 270, LoC::ClassAll, LoC::Common),
		Item_Property(7524,40,4, 8, 250, LoC::ClassAll, LoC::Common),
		Item_Property(7525,50,4, 8, 230, LoC::ClassAll, LoC::Common),
		Item_Property(7526,60,4, 8, 210, LoC::ClassAll, LoC::Common),
		Item_Property(7527,70,4, 8, 190, LoC::ClassAll, LoC::Common),
		Item_Property(7528,80,4, 8, 170, LoC::ClassAll, LoC::Common),
		Item_Property(7529,90,4, 8, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7530,100,4, 8, 130, LoC::ClassAll, LoC::Common),
		Item_Property(7531,1,2, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7532,15,2, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7533,30,2, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7534,40,2, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7535,50,2, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7536,60,2, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7537,70,2, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7538,80,2, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7539,90,2, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7540,100,2, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7541,1,2, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7542,15,2, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7543,30,2, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7544,40,2, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7545,50,2, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7546,60,2, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7547,70,2, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7548,80,2, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7549,90,2, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7550,100,2, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7551,1,2, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7552,15,2, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7553,30,2, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7554,40,2, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7555,50,2, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7556,60,2, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7557,70,2, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7558,80,2, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7559,90,2, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7560,100,2, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7561,1,2, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7562,15,2, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7563,30,2, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7564,40,2, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7565,50,2, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7566,60,2, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7567,70,2, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7568,80,2, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7569,90,2, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7570,100,2, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7571,1,2, 6, 164, LoC::ClassAll, LoC::Common),
		Item_Property(7572,15,2, 6, 150, LoC::ClassAll, LoC::Common),
		Item_Property(7573,30,2, 6, 135, LoC::ClassAll, LoC::Common),
		Item_Property(7574,40,2, 6, 125, LoC::ClassAll, LoC::Common),
		Item_Property(7575,50,2, 6, 115, LoC::ClassAll, LoC::Common),
		Item_Property(7576,60,2, 6, 105, LoC::ClassAll, LoC::Common),
		Item_Property(7577,70,2, 6, 95, LoC::ClassAll, LoC::Common),
		Item_Property(7578,80,2, 6, 85, LoC::ClassAll, LoC::Common),
		Item_Property(7579,90,2, 6, 75, LoC::ClassAll, LoC::Common),
		Item_Property(7580,100,2, 6, 65, LoC::ClassAll, LoC::Common),
		Item_Property(7581,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7582,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7583,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7584,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7585,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7586,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7587,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7588,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7589,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7590,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7591,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7592,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7593,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7594,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7595,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7596,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7597,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7598,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7599,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7600,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7601,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7602,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7603,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7604,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7605,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7606,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7607,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7608,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7609,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7610,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7611,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7612,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7613,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7614,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7615,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7616,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7617,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7618,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7619,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7620,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7621,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7622,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7623,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7624,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7625,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7626,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7627,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7628,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7629,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7630,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7631,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7632,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7633,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7634,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7635,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7636,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7637,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7638,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7639,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7640,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7641,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7642,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7643,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7644,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7645,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7646,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7647,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7648,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7649,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7650,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7651,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7652,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7653,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7654,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7655,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7656,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7657,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7658,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7659,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7660,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7661,1,4, 6, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7662,15,4, 6, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7663,30,4, 6, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7664,40,4, 6, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7665,50,4, 6, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7666,60,4, 6, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7667,70,4, 6, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7668,80,4, 6, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7669,90,4, 6, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7670,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7671,1,4, 6, 24.5, LoC::ClassAll, LoC::Common),
		Item_Property(7672,15,4, 6, 21, LoC::ClassAll, LoC::Common),
		Item_Property(7673,30,4, 6, 17.25, LoC::ClassAll, LoC::Common),
		Item_Property(7674,40,4, 6, 14.75, LoC::ClassAll, LoC::Common),
		Item_Property(7675,50,4, 6, 12.25, LoC::ClassAll, LoC::Common),
		Item_Property(7676,60,4, 6, 9.75, LoC::ClassAll, LoC::Common),
		Item_Property(7677,70,4, 6, 7.25, LoC::ClassAll, LoC::Common),
		Item_Property(7678,80,4, 6, 4.75, LoC::ClassAll, LoC::Common),
		Item_Property(7679,90,4, 6, 2.25, LoC::ClassAll, LoC::Common),
		Item_Property(7680,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7681,1,4, 6, 24.5, LoC::ClassAll, LoC::Common),
		Item_Property(7682,15,4, 6, 21, LoC::ClassAll, LoC::Common),
		Item_Property(7683,30,4, 6, 17.25, LoC::ClassAll, LoC::Common),
		Item_Property(7684,40,4, 6, 14.75, LoC::ClassAll, LoC::Common),
		Item_Property(7685,50,4, 6, 12.25, LoC::ClassAll, LoC::Common),
		Item_Property(7686,60,4, 6, 9.75, LoC::ClassAll, LoC::Common),
		Item_Property(7687,70,4, 6, 7.25, LoC::ClassAll, LoC::Common),
		Item_Property(7688,80,4, 6, 4.75, LoC::ClassAll, LoC::Common),
		Item_Property(7689,90,4, 6, 2.25, LoC::ClassAll, LoC::Common),
		Item_Property(7690,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7691,1,4, 6, 24.5, LoC::ClassAll, LoC::Common),
		Item_Property(7692,15,4, 6, 21, LoC::ClassAll, LoC::Common),
		Item_Property(7693,30,4, 6, 17.25, LoC::ClassAll, LoC::Common),
		Item_Property(7694,40,4, 6, 14.75, LoC::ClassAll, LoC::Common),
		Item_Property(7695,50,4, 6, 12.25, LoC::ClassAll, LoC::Common),
		Item_Property(7696,60,4, 6, 9.75, LoC::ClassAll, LoC::Common),
		Item_Property(7697,70,4, 6, 7.25, LoC::ClassAll, LoC::Common),
		Item_Property(7698,80,4, 6, 4.75, LoC::ClassAll, LoC::Common),
		Item_Property(7699,90,4, 6, 2.25, LoC::ClassAll, LoC::Common),
		Item_Property(7700,100,4, 6, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7701,1,4, 4, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7702,15,4, 4, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7703,30,4, 4, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7704,40,4, 4, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7705,50,4, 4, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7706,60,4, 4, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7707,70,4, 4, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7708,80,4, 4, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7709,90,4, 4, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7710,100,4, 4, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7711,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7712,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7713,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7714,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7715,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7716,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7717,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7718,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7719,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7720,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7721,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7722,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7723,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7724,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7725,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7726,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7727,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7728,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7729,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7730,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7731,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7732,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7733,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7734,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7735,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7736,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7737,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7738,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7739,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7740,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7741,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7742,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7743,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7744,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7745,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7746,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7747,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7748,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7749,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7750,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7751,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7752,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7753,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7754,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7755,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7756,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7757,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7758,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7759,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7760,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7761,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7762,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7763,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7764,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7765,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7766,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7767,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7768,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7769,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7770,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7771,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7772,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7773,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7774,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7775,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7776,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7777,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7778,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7779,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7780,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7781,1,5, 1, 98, LoC::Damage, LoC::Common),
		Item_Property(7782,15,5, 1, 84, LoC::Damage, LoC::Common),
		Item_Property(7783,30,5, 1, 69, LoC::Damage, LoC::Common),
		Item_Property(7784,40,5, 1, 59, LoC::Damage, LoC::Common),
		Item_Property(7785,50,5, 1, 49, LoC::Damage, LoC::Common),
		Item_Property(7786,60,5, 1, 39, LoC::Damage, LoC::Common),
		Item_Property(7787,70,5, 1, 29, LoC::Damage, LoC::Common),
		Item_Property(7788,80,5, 1, 19, LoC::Damage, LoC::Common),
		Item_Property(7789,90,5, 1, 9, LoC::Damage, LoC::Common),
		Item_Property(7790,100,5, 1, 1, LoC::Damage, LoC::Common),
		Item_Property(7791,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7792,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7793,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7794,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7795,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7796,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7797,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7798,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7799,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7800,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7801,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7802,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7803,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7804,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7805,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7806,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7807,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7808,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7809,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7810,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7811,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7812,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7813,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7814,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7815,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7816,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7817,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7818,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7819,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7820,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7821,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7822,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7823,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7824,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7825,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7826,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7827,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7828,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7829,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7830,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7831,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7832,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7833,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7834,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7835,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7836,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7837,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7838,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7839,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7840,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7841,1,5, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7842,15,5, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7843,30,5, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7844,40,5, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7845,50,5, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7846,60,5, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7847,70,5, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7848,80,5, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7849,90,5, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7850,100,5, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7851,1,4, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7852,15,4, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7853,30,4, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7854,40,4, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7855,50,4, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7856,60,4, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7857,70,4, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7858,80,4, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7859,90,4, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7860,100,4, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7861,1,4, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7862,15,4, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7863,30,4, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7864,40,4, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7865,50,4, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7866,60,4, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7867,70,4, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7868,80,4, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7869,90,4, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7870,100,4, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7871,1,4, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7872,15,4, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7873,30,4, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7874,40,4, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7875,50,4, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7876,60,4, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7877,70,4, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7878,80,4, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7879,90,4, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7880,100,4, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7881,1,4, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7882,15,4, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7883,30,4, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7884,40,4, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7885,50,4, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7886,60,4, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7887,70,4, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7888,80,4, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7889,90,4, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7890,100,4, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7891,1,4, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7892,15,4, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7893,30,4, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7894,40,4, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7895,50,4, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7896,60,4, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7897,70,4, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7898,80,4, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7899,90,4, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7900,100,4, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7901,1,4, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7902,15,4, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7903,30,4, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7904,40,4, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7905,50,4, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7906,60,4, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7907,70,4, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7908,80,4, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7909,90,4, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7910,100,4, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7911,1,4, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7912,15,4, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7913,30,4, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7914,40,4, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7915,50,4, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7916,60,4, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7917,70,4, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7918,80,4, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7919,90,4, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7920,100,4, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7921,1,4, 1, 98, LoC::ClassAll, LoC::Common),
		Item_Property(7922,15,4, 1, 84, LoC::ClassAll, LoC::Common),
		Item_Property(7923,30,4, 1, 69, LoC::ClassAll, LoC::Common),
		Item_Property(7924,40,4, 1, 59, LoC::ClassAll, LoC::Common),
		Item_Property(7925,50,4, 1, 49, LoC::ClassAll, LoC::Common),
		Item_Property(7926,60,4, 1, 39, LoC::ClassAll, LoC::Common),
		Item_Property(7927,70,4, 1, 29, LoC::ClassAll, LoC::Common),
		Item_Property(7928,80,4, 1, 19, LoC::ClassAll, LoC::Common),
		Item_Property(7929,90,4, 1, 9, LoC::ClassAll, LoC::Common),
		Item_Property(7930,100,4, 1, 1, LoC::ClassAll, LoC::Common),
		Item_Property(7931,40,6, 6, 50, LoC::Damage, LoC::Unique),
		Item_Property(7932,50,6, 6, 40, LoC::Damage, LoC::Unique),
		Item_Property(7933,60,6, 6, 30, LoC::Damage, LoC::Unique),
		Item_Property(7934,70,6, 6, 20, LoC::Damage, LoC::Unique),
		Item_Property(7935,80,6, 6, 10, LoC::Damage, LoC::Unique),
		Item_Property(7936,90,6, 6, 5, LoC::Damage, LoC::Unique),
		Item_Property(7937,40,6, 6, 50, LoC::Damage, LoC::Unique),
		Item_Property(7938,50,6, 6, 40, LoC::Damage, LoC::Unique),
		Item_Property(7939,60,6, 6, 30, LoC::Damage, LoC::Unique),
		Item_Property(7940,70,6, 6, 20, LoC::Damage, LoC::Unique),
		Item_Property(7941,80,6, 6, 10, LoC::Damage, LoC::Unique),
		Item_Property(7942,90,6, 6, 5, LoC::Damage, LoC::Unique),
		Item_Property(7937,40,6, 6, 50, LoC::Damage, LoC::Unique),
		Item_Property(7938,50,6, 6, 40, LoC::Damage, LoC::Unique),
		Item_Property(7939,60,6, 6, 30, LoC::Damage, LoC::Unique),
		Item_Property(7940,70,6, 6, 20, LoC::Damage, LoC::Unique),
		Item_Property(7941,80,6, 6, 10, LoC::Damage, LoC::Unique),
		Item_Property(7942,90,6, 6, 5, LoC::Damage, LoC::Unique),
		Item_Property(7943,40,6, 6, 50, LoC::Support, LoC::Unique),
		Item_Property(7944,50,6, 6, 40, LoC::Support, LoC::Unique),
		Item_Property(7945,60,6, 6, 30, LoC::Support, LoC::Unique),
		Item_Property(7946,70,6, 6, 20, LoC::Support, LoC::Unique),
		Item_Property(7947,80,6, 6, 10, LoC::Support, LoC::Unique),
		Item_Property(7948,90,6, 6, 5, LoC::Support, LoC::Unique),
		Item_Property(7949,40,6, 6, 50, LoC::Support, LoC::Unique),
		Item_Property(7950,50,6, 6, 40, LoC::Support, LoC::Unique),
		Item_Property(7951,60,6, 6, 30, LoC::Support, LoC::Unique),
		Item_Property(7952,70,6, 6, 20, LoC::Support, LoC::Unique),
		Item_Property(7953,80,6, 6, 10, LoC::Support, LoC::Unique),
		Item_Property(7954,90,6, 6, 5, LoC::Support, LoC::Unique),
		Item_Property(7949,40,6, 6, 50, LoC::Caster, LoC::Unique),
		Item_Property(7950,50,6, 6, 40, LoC::Caster, LoC::Unique),
		Item_Property(7951,60,6, 6, 30, LoC::Caster, LoC::Unique),
		Item_Property(7952,70,6, 6, 20, LoC::Caster, LoC::Unique),
		Item_Property(7953,80,6, 6, 10, LoC::Caster, LoC::Unique),
		Item_Property(7954,90,6, 6, 5, LoC::Caster, LoC::Unique),
		Item_Property(7955,40,6, 6, 50, LoC::Caster, LoC::Unique),
		Item_Property(7956,50,6, 6, 40, LoC::Caster, LoC::Unique),
		Item_Property(7957,60,6, 6, 30, LoC::Caster, LoC::Unique),
		Item_Property(7958,70,6, 6, 20, LoC::Caster, LoC::Unique),
		Item_Property(7959,80,6, 6, 10, LoC::Caster, LoC::Unique),
		Item_Property(7960,90,6, 6, 5, LoC::Caster, LoC::Unique),
		Item_Property(7961,40,6, 6, 50, LoC::Damage, LoC::Unique),
		Item_Property(7962,50,6, 6, 40, LoC::Damage, LoC::Unique),
		Item_Property(7963,60,6, 6, 30, LoC::Damage, LoC::Unique),
		Item_Property(7964,70,6, 6, 20, LoC::Damage, LoC::Unique),
		Item_Property(7965,80,6, 6, 10, LoC::Damage, LoC::Unique),
		Item_Property(7966,90,6, 6, 5, LoC::Damage, LoC::Unique),
		Item_Property(7967,40,6, 6, 50, LoC::Caster, LoC::Unique),
		Item_Property(7968,50,6, 6, 40, LoC::Caster, LoC::Unique),
		Item_Property(7969,60,6, 6, 30, LoC::Caster, LoC::Unique),
		Item_Property(7970,70,6, 6, 20, LoC::Caster, LoC::Unique),
		Item_Property(7971,80,6, 6, 10, LoC::Caster, LoC::Unique),
		Item_Property(7972,90,6, 6, 5, LoC::Caster, LoC::Unique),
		Item_Property(7973,40,6, 6, 50, LoC::Tank, LoC::Unique),
		Item_Property(7974,50,6, 6, 40, LoC::Tank, LoC::Unique),
		Item_Property(7975,60,6, 6, 30, LoC::Tank, LoC::Unique),
		Item_Property(7976,70,6, 6, 20, LoC::Tank, LoC::Unique),
		Item_Property(7977,80,6, 6, 10, LoC::Tank, LoC::Unique),
		Item_Property(7978,90,6, 6, 5, LoC::Damage, LoC::Unique),
		Item_Property(7979,40,6, 6, 50, LoC::Damage, LoC::Unique),
		Item_Property(7980,50,6, 6, 40, LoC::Damage, LoC::Unique),
		Item_Property(7981,60,6, 6, 30, LoC::Damage, LoC::Unique),
		Item_Property(7982,70,6, 6, 20, LoC::Damage, LoC::Unique),
		Item_Property(7983,80,6, 6, 10, LoC::Damage, LoC::Unique),
		Item_Property(7984,90,6, 6, 5, LoC::Damage, LoC::Unique),
		Item_Property(7985,40,6, 6, 50, LoC::Damage, LoC::Unique),
		Item_Property(7986,50,6, 6, 40, LoC::Damage, LoC::Unique),
		Item_Property(7987,60,6, 6, 30, LoC::Damage, LoC::Unique),
		Item_Property(7988,70,6, 6, 20, LoC::Damage, LoC::Unique),
		Item_Property(7989,80,6, 6, 10, LoC::Damage, LoC::Unique),
		Item_Property(7990,90,6, 6, 5, LoC::Damage, LoC::Unique),
		Item_Property(7985,40,6, 6, 50, LoC::Tank, LoC::Unique),
		Item_Property(7986,50,6, 6, 40, LoC::Tank, LoC::Unique),
		Item_Property(7987,60,6, 6, 30, LoC::Tank, LoC::Unique),
		Item_Property(7988,70,6, 6, 20, LoC::Tank, LoC::Unique),
		Item_Property(7989,80,6, 6, 10, LoC::Tank, LoC::Unique),
		Item_Property(7990,90,6, 6, 5, LoC::Tank, LoC::Unique),
		Item_Property(7991,40,6, 6, 50, LoC::Support, LoC::Unique),
		Item_Property(7992,50,6, 6, 40, LoC::Support, LoC::Unique),
		Item_Property(7993,60,6, 6, 30, LoC::Support, LoC::Unique),
		Item_Property(7994,70,6, 6, 20, LoC::Support, LoC::Unique),
		Item_Property(7995,80,6, 6, 10, LoC::Support, LoC::Unique),
		Item_Property(7996,90,6, 6, 5, LoC::Support, LoC::Unique),
		Item_Property(7997,40,6, 6, 50, LoC::Caster, LoC::Unique),
		Item_Property(7998,50,6, 6, 40, LoC::Caster, LoC::Unique),
		Item_Property(7999,60,6, 6, 30, LoC::Caster, LoC::Unique),
		Item_Property(8000,70,6, 6, 20, LoC::Caster, LoC::Unique),
		Item_Property(8001,80,6, 6, 10, LoC::Caster, LoC::Unique),
		Item_Property(8002,90,6, 6, 5, LoC::Caster, LoC::Unique),
		Item_Property(8003,40,6, 6, 50, LoC::Tank, LoC::Unique),
		Item_Property(8004,50,6, 6, 40, LoC::Tank, LoC::Unique),
		Item_Property(8005,60,6, 6, 30, LoC::Tank, LoC::Unique),
		Item_Property(8006,70,6, 6, 20, LoC::Tank, LoC::Unique),
		Item_Property(8007,80,6, 6, 10, LoC::Tank, LoC::Unique),
		Item_Property(8008,90,6, 6, 5, LoC::Tank, LoC::Unique),
		/* END OF AUTOGENERATED */
	};

	int item_min = item_level;
	int item_max = item_level;
	if (rarity == LoC::Common) { item_min -= 6; item_max += 1; }
	if (rarity == LoC::Uncommon) { item_min -= 3; item_max += 4; }
	if (rarity == LoC::Rare) { item_min += 4; item_max += 8; }
	if (rarity == LoC::Legendary) { item_min += 8; item_max += 12; }
	if (rarity == LoC::Unique) { item_min += 9; item_max += 15; }

	
	int weapon_min = item_min;
	int weapon_max = item_max;

	//penalize non damage dealer weaps
	//if (class_type == LoC::Caster) { weapon_min -= 30; weapon_max -= 30; }
	//if (class_type == LoC::Support) { weapon_min -= 20; weapon_max -= 20; }
	//if (class_type == LoC::Tank) { weapon_min -= 10; weapon_max -= 10; }
	
	if (weapon_min < 2) weapon_min = 2;
	if (weapon_max < 2) weapon_max = 4;
	/*if (slot_type == LoC::SlotOneHB || slot_type == LoC::SlotOneHP || slot_type == LoC::SlotOneHS) {
		//1 handers do half dmg
		weapon_min /= 2;
		weapon_max /= 2;
	}*/

	int tmp_level_min = item_min;
	int tmp_level_max = item_max;

	//don't add properties to lesser gear on farther aug indexes
	if (rarity != LoC::Legendary && aug_index == 6) return 0; //no slot 5's on non-legendary
	if ((rarity == LoC::Common || rarity == LoC::Uncommon) && aug_index == 5) return 0; //no skillmods on on common/uncommons.
	if ((rarity == LoC::Common) && aug_index == 4) return 0; //no slot 4 skillmods on common.	

	std::map <int, int> pool;
	int pid = 0;

	if (aug_index == 5) { //all type 5's are rare, so chances of not getting one are pretty high.
		if (rarity == LoC::Common) { pid += 10000; pool[pid] = 0; }
		if (rarity == LoC::Uncommon) { pid += 9000; pool[pid] = 0; } //you shouldn't see epics on uncommon or worse
		if (rarity == LoC::Rare) { pid += 5000; pool[pid] = 0; } //keeping 5k+ rare
		if (rarity == LoC::Unique) { pid += (400 - (GetLevel() * 2)) + 100; pool[pid] = 0; } //2k too high
		if (rarity == LoC::Legendary) { pid +=  (200 - (GetLevel()*2)) + 40; pool[pid] = 0; } //500 too high
	}

	for (auto&& i : items) {
		if (i.slot_group_type == LoC::SlotGroupWeapons && (i.level > weapon_max || i.level < weapon_min)) continue;
		if (i.slot_group_type != LoC::SlotGroupWeapons && (i.level > item_max || i.level < item_min)) continue;

		if (aug_index != i.aug_index) continue;
		if (i.slot_group_type == LoC::SlotGroupBody &&
			slot_type != LoC::SlotChest &&
			slot_type != LoC::SlotLegs &&
			slot_type != LoC::SlotArms) continue;
		if (i.slot_group_type == LoC::SlotGroupHands &&
			slot_type != LoC::SlotHand) continue;
		if (i.slot_group_type == LoC::SlotGroupBoots &&
			slot_type != LoC::SlotFeet) continue;
		if (i.slot_group_type == LoC::SlotGroupBelt &&
			slot_type != LoC::SlotWaist) continue;
		if (i.slot_group_type == LoC::SlotGroupHead &&
			slot_type != LoC::SlotHead) continue;
		if (i.slot_group_type == LoC::SlotGroupWeapons &&
			slot_type != LoC::SlotOneHB &&
			slot_type != LoC::SlotTwoHB &&
			slot_type != LoC::SlotOneHP &&
			slot_type != LoC::SlotTwoHP &&
			slot_type != LoC::SlotOneHS &&
			slot_type != LoC::SlotTwoHS &&
			slot_type != LoC::SlotOneHP &&
			slot_type != LoC::SlotArchery &&
			slot_type != LoC::SlotThrowing) continue;
		if (i.slot_group_type == LoC::SlotGroupJewelry &&
			slot_type != LoC::SlotFingers &&
			slot_type != LoC::SlotNeck) continue;
		if (i.class_type != LoC::ClassAll && i.class_type != class_type) continue; //if class filter is set to non-all
		if (i.rarity_type > rarity) continue; //if the rarity of property is set lower than this item being generated

		
		pid += i.weight;
		pool[pid] = i.item_id;
	}
	if (pid == 0) return 0; //no properties match criteria

	int dice = zone->random.Int(1, pid);

	int lastPool = 0;
	for (auto entry = pool.begin(); entry != pool.end(); ++entry) {
		if (dice > entry->first) {
			lastPool = entry->first;
			continue;
		}
		return entry->second;
	}
	return 0; //no item found
}
