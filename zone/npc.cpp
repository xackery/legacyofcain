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

	if (GetLevel() >= 66) {
		if (min_dmg==0)
			min_dmg = 220;
		if (max_dmg==0)
			max_dmg = ((((99000)*(GetLevel()-64))/400)*AC_adjust/10);
	}
	else if (GetLevel() >= 60 && GetLevel() <= 65){
		if(min_dmg==0)
			min_dmg = (GetLevel()+(GetLevel()/3));
		if(max_dmg==0)
			max_dmg = (GetLevel()*3)*AC_adjust/10;
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
	AdjustStats(d, in_respawn);
	AddAbilities(d, in_respawn);
	
}


//GetRandomize type returns what type of randomization should be done to the mob. 
//Currently, we just return 1 if the mob is a roaming monster.
int NPC::GetRandomizeType(const NPCType* d, Spawn2* in_respawn) {
	if (!in_respawn) return 0;

	if (in_respawn->SpawnGroupID() >= 1151 && in_respawn->SpawnGroupID() <= 1256) return 1;	//feerrott
	return 0;
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

	//Level was determined based on the spawngroup, we now adjust it's level with category
	int levelMod = 0;

	if (cat == LoC::MobChampion) levelMod = zone->random.Int(1, 5);
	if (cat == LoC::MobRare) levelMod = zone->random.Int(3, 8);
	if (cat == LoC::MobUnique) levelMod = zone->random.Int(5, 8);
	if (cat == LoC::MobSuperUnique) levelMod = zone->random.Int(9, 15);
	if (cat == LoC::MobBoss) levelMod = zone->random.Int(8, 20);

	SetLevel(level + levelMod);

	roambox_delay = zone->random.Int(1000, 10000);
	roambox_min_delay = zone->random.Int(500, 1000);
	roambox_distance = zone->random.Int(5, 50);
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
	if (cat < 1) return; //don't itemize non-categorized npcs.

	int difficulty = zone->GetInstanceID();

	int drop_count = GetDropCount(killer);	
	if (drop_count == 0) return;

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
		if (slot_type == LoC::Hands) slot_type = zone->random.Int(0, LoC::SlotHandsMax);	 //hands are all weapon types, so RNG considers all weapons same grouping
		int class_type = zone->random.Int(0, LoC::ClassMax);
		int item_level = GetItemLevel(killer);
		item_id = GetItemBase(rarity, slot_type, class_type, item_level);
		//aug1_id = GetItemProperty(1, rarity, slot_type, class_type, item_level);
		aug2_id = GetItemProperty(1, rarity, slot_type, class_type, item_level);
		aug3_id = GetItemProperty(1, rarity, slot_type, class_type, item_level);
		aug4_id = GetItemProperty(1, rarity, slot_type, class_type, item_level);
		aug5_id = GetItemProperty(1, rarity, slot_type, class_type, item_level);
		aug6_id = GetItemProperty(1, rarity, slot_type, class_type, item_level);

		if (item_id == 0) return;
		AddItem(item_id, 1, false, aug1_id, aug2_id, aug3_id, aug4_id, aug5_id, aug6_id);
	}
	return;
}



int NPC::GetItemLevel(Mob *killer) {
	int item_level = 0;

	return item_level;
}


//GetDropCount determines how many drops a mob dropped, and is called by DoItemization
int NPC::GetDropCount(Mob *killer) {
	int difficulty = zone->GetInstanceID();
	//Drop count is weighted to lean towards less more than more.
	int drop_1_chance = 0;
	if (difficulty == LoC::Normal) drop_1_chance = RuleI(NPC, ItemDrop1ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_1_chance = RuleI(NPC, ItemDrop1ChanceNightmare);
	if (difficulty == LoC::Hell) drop_1_chance = RuleI(NPC, ItemDrop1ChanceHell);
	int drop_2_chance = 0;
	if (difficulty == LoC::Normal) drop_2_chance = RuleI(NPC, ItemDrop2ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_2_chance = RuleI(NPC, ItemDrop2ChanceNightmare);
	if (difficulty == LoC::Hell) drop_2_chance = RuleI(NPC, ItemDrop2ChanceHell);
	int drop_3_chance = 0;
	if (difficulty == LoC::Normal) drop_3_chance = RuleI(NPC, ItemDrop3ChanceNormal);
	if (difficulty == LoC::Nightmare) drop_3_chance = RuleI(NPC, ItemDrop3ChanceNightmare);
	if (difficulty == LoC::Hell) drop_3_chance = RuleI(NPC, ItemDrop3ChanceHell);
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

	int common_chance = 0;
	if (difficulty == LoC::Normal) common_chance = RuleI(NPC, ItemCommonChanceNormal);
	if (difficulty == LoC::Nightmare) common_chance = RuleI(NPC, ItemCommonChanceNightmare);
	if (difficulty == LoC::Hell) common_chance = RuleI(NPC, ItemCommonChanceHell);
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



bool NPC::AddPrefix(int counter, int prefix) {
	if (prefix == LoC::PrefixGloom) {
		SetName(StringFormat("%sloom %s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1400;
	}
	if (prefix == LoC::PrefixGray) {
		SetName(StringFormat("%sray %s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1401;
	}
	if (prefix == LoC::PrefixDire) {
		SetName(StringFormat("%sire %s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1402;
	}
	if (prefix == LoC::PrefixBlack) {
		SetName(StringFormat("%slack %s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1403;
	}
	if (prefix == LoC::PrefixShadow) {
		SetName(StringFormat("%shadow %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1404;
	}
	if (prefix == LoC::PrefixHaze) {
		SetName(StringFormat("%saze %s", (counter == 0) ? "H" : "h", name).c_str());
		npc_spells_id = 1405;
	}
	if (prefix == LoC::PrefixWind) {
		SetName(StringFormat("%sind %s", (counter == 0) ? "W" : "w", name).c_str());
		npc_spells_id = 1406;
	}
	if (prefix == LoC::PrefixStorm) {
		SetName(StringFormat("%storm %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1407;
	}
	if (prefix == LoC::PrefixWarp) {
		SetName(StringFormat("%sarp %s", (counter == 0) ? "W" : "w", name).c_str());
		npc_spells_id = 1408;
	}
	if (prefix == LoC::PrefixNight) {
		SetName(StringFormat("%sight %s", (counter == 0) ? "N" : "n", name).c_str());
		npc_spells_id = 1409;
	}
	if (prefix == LoC::PrefixMoon) {
		SetName(StringFormat("%soon %s", (counter == 0) ? "M" : "m", name).c_str());
		npc_spells_id = 1410;
	}
	if (prefix == LoC::PrefixStar) {
		SetName(StringFormat("%star %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1411;
	}
	if (prefix == LoC::PrefixPit) {
		SetName(StringFormat("%sit %s", (counter == 0) ? "P" : "p", name).c_str());
		npc_spells_id = 1412;
	}
	if (prefix == LoC::PrefixFire) {
		SetName(StringFormat("%sire %s", (counter == 0) ? "F" : "f", name).c_str());
		npc_spells_id = 1413;
	}
	if (prefix == LoC::PrefixCold) {
		SetName(StringFormat("%sold %s", (counter == 0) ? "C" : "c", name).c_str());
		npc_spells_id = 1414;
	}
	if (prefix == LoC::PrefixSeethe) {
		SetName(StringFormat("%seethe %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1415;
	}
	if (prefix == LoC::PrefixSharp) {
		SetName(StringFormat("%sharp %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1416;
	}
	if (prefix == LoC::PrefixAsh) {
		SetName(StringFormat("%ssh %s", (counter == 0) ? "A" : "a", name).c_str());
		npc_spells_id = 1417;
	}
	if (prefix == LoC::PrefixBlade) {
		SetName(StringFormat("%slade %s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1418;
	}
	if (prefix == LoC::PrefixSteel) {
		SetName(StringFormat("%steel %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1419;
	}
	if (prefix == LoC::PrefixStone) {
		SetName(StringFormat("%stone %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1420;
	}
	if (prefix == LoC::PrefixRust) {
		SetName(StringFormat("%sust %s", (counter == 0) ? "R" : "r", name).c_str());
		npc_spells_id = 1421;
	}
	if (prefix == LoC::PrefixMold) {
		SetName(StringFormat("%sold %s", (counter == 0) ? "M" : "m", name).c_str());
		npc_spells_id = 1422;
	}
	if (prefix == LoC::PrefixBlight) {
		SetName(StringFormat("%slight %s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1423;
	}
	if (prefix == LoC::PrefixPlague) {
		SetName(StringFormat("%slague %s", (counter == 0) ? "P" : "p", name).c_str());
		npc_spells_id = 1424;
	}
	if (prefix == LoC::PrefixRot) {
		SetName(StringFormat("%sot %s", (counter == 0) ? "R" : "r", name).c_str());
		npc_spells_id = 1425;
	}
	if (prefix == LoC::PrefixOoze) {
		SetName(StringFormat("%soze %s", (counter == 0) ? "O" : "o", name).c_str());
		npc_spells_id = 1426;
	}
	if (prefix == LoC::PrefixPuke) {
		SetName(StringFormat("%suke %s", (counter == 0) ? "P" : "p", name).c_str());
		npc_spells_id = 1427;
	}
	if (prefix == LoC::PrefixSnot) {
		SetName(StringFormat("%snot %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1428;
	}
	if (prefix == LoC::PrefixBile) {
		SetName(StringFormat("%sile %s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1429;
	}
	if (prefix == LoC::PrefixBlood) {
		SetName(StringFormat("%slood %s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1430;
	}
	if (prefix == LoC::PrefixPulse) {
		SetName(StringFormat("%sulse %s", (counter == 0) ? "P" : "p", name).c_str());
		npc_spells_id = 1431;
	}
	if (prefix == LoC::PrefixGut) {
		SetName(StringFormat("%sut %s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1432;
	}
	if (prefix == LoC::PrefixGore) {
		SetName(StringFormat("%sore %s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1433;
	}
	if (prefix == LoC::PrefixFlesh) {
		SetName(StringFormat("%slesh %s", (counter == 0) ? "F" : "f", name).c_str());
		npc_spells_id = 1434;
	}
	if (prefix == LoC::PrefixBone) {
		SetName(StringFormat("%sone %s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1435;
	}
	if (prefix == LoC::PrefixSpine) {
		SetName(StringFormat("%spine %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1436;
	}
	if (prefix == LoC::PrefixMind) {
		SetName(StringFormat("%sind %s", (counter == 0) ? "M" : "m", name).c_str());
		npc_spells_id = 1437;
	}
	if (prefix == LoC::PrefixSpirit) {
		SetName(StringFormat("%spirit %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1438;
	}
	if (prefix == LoC::PrefixSoul) {
		SetName(StringFormat("%soul %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1439;
	}
	if (prefix == LoC::PrefixWrath) {
		SetName(StringFormat("%srath %s", (counter == 0) ? "W" : "w", name).c_str());
		npc_spells_id = 1440;
	}
	if (prefix == LoC::PrefixGrief) {
		SetName(StringFormat("%srief %s", (counter == 0) ? "G" : "g", name).c_str());
		npc_spells_id = 1441;
	}
	if (prefix == LoC::PrefixFoul) {
		SetName(StringFormat("%soul %s", (counter == 0) ? "F" : "f", name).c_str());
		npc_spells_id = 1442;
	}
	if (prefix == LoC::PrefixVile) {
		SetName(StringFormat("%sile %s", (counter == 0) ? "V" : "v", name).c_str());
		npc_spells_id = 1443;
	}
	if (prefix == LoC::PrefixSin) {
		SetName(StringFormat("%sin %s", (counter == 0) ? "S" : "s", name).c_str());
		npc_spells_id = 1444;
	}
	if (prefix == LoC::PrefixChaos) {
		SetName(StringFormat("%shaos %s", (counter == 0) ? "C" : "c", name).c_str());
		npc_spells_id = 1445;
	}
	if (prefix == LoC::PrefixDread) {
		SetName(StringFormat("%sread %s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1446;
	}
	if (prefix == LoC::PrefixDoom) {
		SetName(StringFormat("%soom %s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1447;
	}
	if (prefix == LoC::PrefixBane) {
		SetName(StringFormat("%sane %s", (counter == 0) ? "B" : "b", name).c_str());
		npc_spells_id = 1448;
	}
	if (prefix == LoC::PrefixDeath) {
		SetName(StringFormat("%seath %s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1449;
	}
	if (prefix == LoC::PrefixViper) {
		SetName(StringFormat("%siper %s", (counter == 0) ? "V" : "v", name).c_str());
		npc_spells_id = 1450;
	}
	if (prefix == LoC::PrefixDragon) {
		SetName(StringFormat("%sragon %s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1451;
	}
	if (prefix == LoC::PrefixDevil) {
		SetName(StringFormat("%sevil %s", (counter == 0) ? "D" : "d", name).c_str());
		npc_spells_id = 1452;
	}

	return true;
}

bool NPC::AddSuffix(int suffix) {
	if (suffix == LoC::SuffixTouch) {
		SetName(StringFormat("%s Touch", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSpell) {
		SetName(StringFormat("%s Spell", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFeast) {
		SetName(StringFormat("%s Feast", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWound) {
		SetName(StringFormat("%s Wound", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixGrin) {
		SetName(StringFormat("%s Grin", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMaim) {
		SetName(StringFormat("%s Maim", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHack) {
		SetName(StringFormat("%s Hack", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBite) {
		SetName(StringFormat("%s Bite", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixRend) {
		SetName(StringFormat("%s Rend", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBurn) {
		SetName(StringFormat("%s Burn", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixRip) {
		SetName(StringFormat("%s Rip", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixKill) {
		SetName(StringFormat("%s Kill", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCall) {
		SetName(StringFormat("%s Call", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixVex) {
		SetName(StringFormat("%s Vex", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixJade) {
		SetName(StringFormat("%s Jade", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWeb) {
		SetName(StringFormat("%s Web", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixShield) {
		SetName(StringFormat("%s Shield", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixKiller) {
		SetName(StringFormat("%s Killer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixRazor) {
		SetName(StringFormat("%s Razor", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDrinker) {
		SetName(StringFormat("%s Drinker", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixShifter) {
		SetName(StringFormat("%s Shifter", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCrawler) {
		SetName(StringFormat("%s Crawler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDancer) {
		SetName(StringFormat("%s Dancer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBender) {
		SetName(StringFormat("%s Bender", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWeaver) {
		SetName(StringFormat("%s Weaver", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixEater) {
		SetName(StringFormat("%s Eater", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWidow) {
		SetName(StringFormat("%s Widow", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMaggot) {
		SetName(StringFormat("%s Maggot", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSpawn) {
		SetName(StringFormat("%s Spawn", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWight) {
		SetName(StringFormat("%s Wight", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixGrumble) {
		SetName(StringFormat("%s Grumble", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixGrowler) {
		SetName(StringFormat("%s Growler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSnarl) {
		SetName(StringFormat("%s Snarl", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWolf) {
		SetName(StringFormat("%s Wolf", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCrow) {
		SetName(StringFormat("%s Crow", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHawk) {
		SetName(StringFormat("%s Hawk", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCloud) {
		SetName(StringFormat("%s Cloud", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBang) {
		SetName(StringFormat("%s Bang", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHead) {
		SetName(StringFormat("%s Head", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSkull) {
		SetName(StringFormat("%s Skull", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBrow) {
		SetName(StringFormat("%s Brow", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixEye) {
		SetName(StringFormat("%s Eye", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMaw) {
		SetName(StringFormat("%s Maw", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixTongue) {
		SetName(StringFormat("%s Tongue", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFang) {
		SetName(StringFormat("%s Fang", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHorn) {
		SetName(StringFormat("%s Horn", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixThorn) {
		SetName(StringFormat("%s Thorn", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixClaw) {
		SetName(StringFormat("%s Claw", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFist) {
		SetName(StringFormat("%s Fist", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHeart) {
		SetName(StringFormat("%s Heart", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixShank) {
		SetName(StringFormat("%s Shank", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSkin) {
		SetName(StringFormat("%s Skin", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWing) {
		SetName(StringFormat("%s Wing", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixPox) {
		SetName(StringFormat("%s Pox", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFester) {
		SetName(StringFormat("%s Fester", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBlister) {
		SetName(StringFormat("%s Blister", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixPus) {
		SetName(StringFormat("%s Pus", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSlime) {
		SetName(StringFormat("%s Slime", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDrool) {
		SetName(StringFormat("%s Drool", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFroth) {
		SetName(StringFormat("%s Froth", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSludge) {
		SetName(StringFormat("%s Sludge", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixVenom) {
		SetName(StringFormat("%s Venom", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixPoison) {
		SetName(StringFormat("%s Poison", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixBreak) {
		SetName(StringFormat("%s Break", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixShard) {
		SetName(StringFormat("%s Shard", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFlame) {
		SetName(StringFormat("%s Flame", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMaul) {
		SetName(StringFormat("%s Maul", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixThirst) {
		SetName(StringFormat("%s Thirst", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixLust) {
		SetName(StringFormat("%s Lust", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHammer) {
		SetName(StringFormat("%s the Hammer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixAxe) {
		SetName(StringFormat("%s the Axe", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSharp) {
		SetName(StringFormat("%s the Sharp", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixJagged) {
		SetName(StringFormat("%s the Jagged", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFlayer) {
		SetName(StringFormat("%s the Flayer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSlasher) {
		SetName(StringFormat("%s the Slasher", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixImpaler) {
		SetName(StringFormat("%s the Impaler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHunter) {
		SetName(StringFormat("%s the Hunter", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSlayer) {
		SetName(StringFormat("%s the Slayer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMauler) {
		SetName(StringFormat("%s the Mauler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDestroyer) {
		SetName(StringFormat("%s the Destroyer", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixQuick) {
		SetName(StringFormat("%s the Quick", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWitch) {
		SetName(StringFormat("%s the Witch", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMad) {
		SetName(StringFormat("%s the Mad", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixWraith) {
		SetName(StringFormat("%s the Wraith", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixShade) {
		SetName(StringFormat("%s the Shade", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDead) {
		SetName(StringFormat("%s the Dead", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixUnholy) {
		SetName(StringFormat("%s the Unholy", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHowler) {
		SetName(StringFormat("%s the Howler", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixGrim) {
		SetName(StringFormat("%s the Grim", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixDark) {
		SetName(StringFormat("%s the Dark", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixTainted) {
		SetName(StringFormat("%s the Tainted", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixUnclean) {
		SetName(StringFormat("%s the Unclean", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixHungry) {
		SetName(StringFormat("%s the Hungry", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCold) {
		SetName(StringFormat("%s the Cold", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixAuraEnchanted) {
		SetName(StringFormat("%s Aura Enchanted", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixColdEnchanted) {
		SetName(StringFormat("%s Cold Enchanted", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixCursed) {
		SetName(StringFormat("%s Cursed", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixExtraFast) {
		SetName(StringFormat("%s Extra Fast", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixExtraStrong) {
		SetName(StringFormat("%s Extra Strong", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixFireEnchanted) {
		SetName(StringFormat("%s Fire Enchanted", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixLightningEnchanted) {
		SetName(StringFormat("%s Lightning Enchanted", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMagicResistant) {
		SetName(StringFormat("%s Magic Resistant", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixManaBurn) {
		SetName(StringFormat("%s Mana Burn", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixMultiShot) {
		SetName(StringFormat("%s Multi-Shot", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixSpectralHit) {
		SetName(StringFormat("%s Spectral Hit", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixStoneSkin) {
		SetName(StringFormat("%s Stone Skin", name).c_str());
		npc_spells_id = 1;
	}
	if (suffix == LoC::SuffixTeleporting) {
		SetName(StringFormat("%s Teleporting", name).c_str());
		npc_spells_id = 1;
	}
	return true;
}