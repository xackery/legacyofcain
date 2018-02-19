sub GM_EVENT_SAY{
	my $text = plugin::val('$text');
	my $entity_list = plugin::val('$entity_list');
	my $client = plugin::val('$client');
	my $npc = plugin::val('$npc');
	my $mob = plugin::val('$mob');
	my $zoneid = plugin::val('$zoneid');
	my @arg = split(' ', $text);

	if($text=~/#dev/i){
		if(!$arg[1]){ 
			$client->Message(15, "Developer tools"); 
			$client->Message(15, "Zonewide: [" . quest::saylink("#dev setzoneinvis 1", 1, "Set Invis NPC's to Target") . " - " . quest::saylink("#dev setzoneinvis 2", 1, "NoTarget") . " ] [" . quest::saylink("#scale levelsave", 1, "Write all current NPC Levels to DB") . "]"); 
			$client->Message(15, "#dev tools - Miscellaneous tools");
			$client->Message(15, "#dev findnpcs (npcname)");
			$client->Message(15, "#dev killallnpcs - Kills all NPC's in the zone");
			$client->Message(15, "#dev killnpcs (name) - Kills all NPC's in the zone with specified name");
			$client->Message(15, "#dev npcedit (will bring up EoC Window for targeted NPC)");
			$client->Message(15, "#dev setzonerespawn [long (week long) - time in seconds] - Sets the entire zone to specified respawn time");
			$client->Message(15, "#dev createtour [start_pop_id] [glide_mod]");
			$client->Message(15, "#dev addtourpoint [tour_id]");
			$client->Message(15, "#dev showtour [tour_id]");
			$client->Message(15, "#dev remove");
			$client->Message(15, "#dev recordkillcount");
		}
		if($arg[1] eq "killnpcs"){
			my @nlist = $entity_list->GetNPCList();
			if($arg[2] ne ""){
				$nname = $arg[2];
				foreach my $n (@nlist){ 
					if($n->GetCleanName()=~/$nname/i){
						$n->Damage($client, 10, 0, 0); $n->Damage($client, 1000000000, 0, 0); 
					}
				}
			}
			else{
				$client->Message(15, "You must specify an NPC name");
			}
		}
		if($arg[1] eq "killallnpcs"){
			my @nlist = $entity_list->GetNPCList();
			foreach my $n (@nlist){ 
				$n->Damage($client, 10, 0, 0); $n->Damage($client, 1000000000, 0, 0); 
			}
		}
		if($arg[1] eq "npcedit"){
			my $Target = $client->GetTarget(); my $TargetNPCID = $Target->GetNPCTypeID(); my $TargName = $Target->GetCleanName();
			plugin::DiaWind("<a href=\"http://eoc.akkadius.com/AC/eoc/min.php?Mod=NPC&SingleNPCEdit=" . $TargetNPCID . "\">Edit - " . $TargName. "</a> mysterious");
		}
		if($arg[1] eq "recordkillcount"){
			$client->Message(15, "Your kills will now temporarily be record to DB table `cust_temp_kill_count` to measure how many of what NPC's you kill in a zone (Useful for Expedition measuring)");
			$client->SetEntityVariable("recordkillcount", 1);
		}
		if($arg[1] eq "createtour"){
			$connect = plugin::LoadMysql();
			if($arg[2] && $arg[3]){
				$query = "INSERT INTO `cust_tour_def` (start_pop_id, glide_mod) VALUES (?, ?)";
				$query_handle = $connect->prepare($query); $query_handle->execute($arg[2], $arg[3]);
				my $last_id = $query_handle->{mysql_insertid};
				if($Debug){ $client->Message(15, "DEBUG" . $query . " lastid " . $last_id); }
			}
		}
		if($arg[1] eq "addtourpoint"){
			$connect = plugin::LoadMysql();
			if($arg[2]){
				$query = "SELECT `step` FROM `cust_tour_entries` WHERE `tour_id` = ? ORDER BY `step` DESC LIMIT 1";
				$query_handle = $connect->prepare($query); $query_handle->execute($arg[2]);
				while (@row = $query_handle->fetchrow_array()){ $ltp = ($row[0] + 1); }
				if(!$ltp){ $ltp = 1; }
				$client->Message(15, "DEBUG Last Tour Point " . $ltp); 
				$query = "INSERT INTO `cust_tour_entries` (tour_id, step, x, y, z, h, popup_text) 
				VALUES (?, ?, ?, ?, ?, ?, ?)";
				$query_handle = $connect->prepare($query); 
				$query_handle->execute($arg[2], $ltp, $client->GetX(), $client->GetY(), $client->GetZ(), $client->GetHeading(), "");
			}
		}
		if($arg[1] eq "remove"){
			if(!$arg[2]){
				$client->Message(15, "zonecontroller duplicates - Removes zonecontroller duplicates (Akkadius)");
			}
			if($arg[2]=~/zonecontroller/i){
				$connect = plugin::LoadMysql();
				$connect2 = plugin::LoadMysql();
				$query = "SELECT
					spawn2.id,
					npc_types.name,
					spawn2.zone,
					spawn2.`version`
					FROM
					spawn2
					Inner Join spawnentry ON spawnentry.spawngroupID = spawn2.spawngroupID
					Inner Join npc_types ON npc_types.id = spawnentry.npcID
					WHERE npc_types.id = 50
					ORDER by spawn2.zone, spawn2.`version`";
				$query_handle = $connect->prepare($query); $query_handle->execute();
				while (@row = $query_handle->fetchrow_array()){
					#$client->Message(15, $row[0] . " " . $row[1] . " " . $row[2] . " " . $row[3]);
					if($ZCD{$row[2]}[$row[3]]){
						#$client->Message(15, "#::: DUPLICATE!!!");
						$query2 = "DELETE FROM spawn2 WHERE id = " . $row[0] . "";
						$client->Message(15, $query2);
						$query_handle2 = $connect2->prepare($query2); $query_handle2->execute();
					}
					$ZCD{$row[2]}[$row[3]] = 1;
				}
			}
		}
		if($arg[1] eq "findnpcs"){
			$Name = $arg[2]; $n = 0;
			@ent = $entity_list->GetNPCList();
			foreach $NPC (@ent){
				if($NPC->GetName()=~/$Name/i || $arg[2] eq ""){
					$client->Message(15, quest::saylink("#goto " . $NPC->GetX() . " " . $NPC->GetY() . " " .  $NPC->GetZ(), 0, $NPC->GetName()));
					$n++;
				}
			}
			$client->Message(15, $n . " NPC's found");
		}
		if($arg[1] eq "setzonerespawn"){
			$connect = plugin::LoadMysql();
			$connect2 = plugin::LoadMysql();
			if($arg[2] eq "long"){ $respawntime = '21600'; } else{ $respawntime = $arg[2]; }
			$query = "SELECT
				spawnentry.npcID,
				spawn2.zone,
				spawn2.`version`,
				spawn2.respawntime,
				spawn2.spawngroupID
				FROM
				spawnentry
				Inner Join spawn2 ON spawnentry.spawngroupID = spawn2.spawngroupID
				where spawn2.zone = '" . $zonesn . "' and spawnentry.npcID > 0 and spawn2.version = " . $instanceversion . "
				GROUP by spawngroupID";
				$query_handle = $connect->prepare($query); $query_handle->execute();
			$client->Message(15, $query);
			while (@row = $query_handle->fetchrow_array()){
				$query2 = "UPDATE `spawn2` SET `respawntime` = '" . $respawntime . "', `variance` = 0 WHERE spawngroupID = " . $row[4] . "";
				$client->Message(15, $query2);
				$query_handle2 = $connect2->prepare($query2); $query_handle2->execute();
			}
		}
		if($arg[1] eq "setzonefaction"){
			$connect = plugin::LoadMysql();
			$connect2 = plugin::LoadMysql();
			$query = "SELECT
				spawnentry.npcID,
				spawn2.zone,
				spawn2.`version`,
				spawn2.respawntime,
				spawn2.spawngroupID
				FROM
				spawnentry
				Inner Join spawn2 ON spawnentry.spawngroupID = spawn2.spawngroupID
				where spawn2.zone = '" . $zonesn . "' and spawnentry.npcID > 0 and spawn2.version = " . $instanceversion . "
				GROUP by spawngroupID";
				$query_handle = $connect->prepare($query); $query_handle->execute();
			$client->Message(15, $query);
			while (@row = $query_handle->fetchrow_array()){
				$query2 = "UPDATE `npc_types` SET `npc_faction_id` = '" . $arg[2] . "' WHERE id = " . $row[0] . "";
				$client->Message(15, $query2);
				$query_handle2 = $connect2->prepare($query2); $query_handle2->execute();
			}
		}
		if($arg[1] eq "setzoneinvis"){
			if($arg[2]){
				$connect = plugin::LoadMysql();
				$client->Message(15, "Hi");
				if($arg[2] == 1){ $BodyType = 0; $ttype = "targetable"; } 
				if($arg[2] == 2){ $BodyType = 11;  $ttype = "notarget"; } 
				@ent = $entity_list->GetNPCList();
				my @SavedList = undef;
				foreach $NPC (@ent){
					if($NPC->GetRace() == 127 || $NPC->GetRace() == 240){
						$NPCID = $NPC->GetNPCTypeID();
						if(!$SavedList[$NPCID]){
							$SavedList[$NPCID] = 1;
							$query = "UPDATE `npc_types` SET bodytype = " . $BodyType . " WHERE `id` = " . $NPCID;
							$query_handle = $connect->prepare($query); $query_handle->execute();
							$client->Message(15, $query);
						}
					}
				}
				$client->Message(15, "Invisible NPC's set to " . $ttype);
				$client->Message(15, "Repopping Zone " . $ttype);
				quest::repopzone();
			}
			if($arg[1] eq "listnpcs"){
				@ent = $entity_list->GetNPCList();
				my @SavedList = undef;
				$Search = $arg[2];
				foreach $NPC (@ent){
					#if($NPC->GetName()=~/$Search/i){
						$client->Message(15, $NPC->GetName());
					#}
				}
			}
		}
		if($arg[1] eq "npctools"){
			$connect = plugin::LoadMysql();
			my $NPC = $entity_list->GetNPCByID($arg[2]);
			my $NPCID = $NPC->GetNPCTypeID();
			my $NPCID2 = $NPC->GetID();
			
			$client->Message(15, "NPC: " . $NPC->GetCleanName());
			$client->Message(15, "Faction: Set to " . quest::saylink("#dev npctools $NPCID2 setfaction 0", 1, "Indifferent") . " - " . quest::saylink("#dev npctools $NPCID2 setfaction 79", 1, "KOS Assist") . " - " . quest::saylink("#dev npctools $NPCID2 setfaction 623", 1, "KOS Non-Assist"));
			if($arg[3] eq "setfaction"){
				$query = "UPDATE `npc_types` SET npc_faction_id = " . $arg[4] . " WHERE `id` = " . $NPCID;
				$query_handle = $connect->prepare($query); $query_handle->execute();
				$client->Message(15, $query);
				quest::repopzone();
			}
		}
	}
	if($text=~/#show/i){
		if(!$arg[1]){
			$client->Message(15, " playerinst <playername> - Shows what instances a player is in");
			$client->Message(15, " playerexpd <playername> - Shows what expeditions a player is in");
			$client->Message(15, " zonelines - shows zonelines in the zone");
			$client->Message(15, " currencies - shows available item currencies");
			$client->Message(15, " traps - shows traps in the zone");
			$client->Message(15, " grid - shows targeted NPC's grid in form of clickable saylinks");
			$client->Message(15, " gridnpc - shows targeted NPC's grid in form of placeholder skeletons");
			return;
		}
		if($arg[1] eq "cursor"){
			my $item = $client->GetItemIDAt(30);
			if($item < 1000000){ $client->Message(15, "You have an item on your cursor $item: " . quest::varlink($item) . "") }
			else{ $client->Message(15, "There is no item on your cursor"); }
		}
		if($arg[1] eq "gotoinst"){
			quest::AssignToInstance($arg[3]); 
			# MovePCInstance(zoneID, instanceID, x, y, z, heading)
			$client->MovePCInstance($arg[2], $arg[3], $arg[4], $arg[5], $arg[6], 0);
		}
		if($arg[1] eq "jumpintoexp"){
			# 602 - exp
			$client->SignalClient(602);
			$client->SignalClient($arg[2]);
		}
		if($arg[1] eq "gridnpc"){
			if($client->GetTarget()->IsNPC()){
				my $ngrid = $client->GetTarget()->CastToNPC()->GetGrid();
				if($ngrid > 0){
					$connect = plugin::LoadMysql();
					$query = "SELECT x, y, z, heading FROM `grid_entries` WHERE `zoneid` = ? AND `gridid` = ? order by gridid, number";
					$query_handle = $connect->prepare($query); $query_handle->execute($zoneid, $ngrid);
					while (@row = $query_handle->fetchrow_array()){  
						quest::spawn2(614, 0, 0, $row[0], $row[1], $row[2], $row[3]);
					}
					plugin::MM("Loading Grid data for " . $client->GetTarget()->GetCleanName() . " Grid ID " . $ngrid);
				}
				else{ plugin::MM("Target does not have a Grid...");  } 
			}
			else{ plugin::MM("Target is not an NPC...");  }
		}
		if($arg[1] eq "grid"){
			$query = "SELECT
				grid_entries.number,
				grid_entries.x,
				grid_entries.y,
				grid_entries.z,
				grid_entries.heading,
				grid_entries.pause
				FROM
				grid_entries
				where gridid = " . $client->GetTarget()->CastToNPC()->GetGrid() .  " and zoneid = " . $zoneid . "";
				$connect = plugin::LoadMysql();
				$client->Message(15, "Loading Grid...");
				$query_handle = $connect->prepare($query); $query_handle->execute();
				while(@row = $query_handle->fetchrow_array()){
					$client->Message(15, "[" . quest::saylink("#goto " . int($row[1]) . " " . int($row[2]) . " " . int($row[3]), 0, "GO TO") . "] " . $row[0] . " X($row[1]) Y($row[2]) Z($row[3]) H($row[4]) Pause - $row[5] ");
				}
		}
		if($arg[1] eq "playerexpd"){
			if($arg[2]){
				$Where = "and cust_inst_players.player_name LIKE '%" . $arg[2] . "%'";
			}else { $Where = ""; }
				# MovePCInstance(zoneID, instanceID, x, y, z, heading)
				$query = "SELECT
					cust_ext_instances.inst_id,
					cust_ext_instances.identifier,
					cust_ext_instances.type,
					cust_ext_instances.`limit`,
					cust_ext_instances.task_assoc,
					cust_ext_instances.x,
					cust_ext_instances.y,
					cust_ext_instances.z, 
					cust_inst_players.char_id,
					cust_inst_players.player_name,
					cust_inst_players.pending_invite,
					cust_inst_players.is_leader,
					cust_ext_instances.avglevelreq,
					cust_ext_instances.req_players,
					cust_ext_instances.shared_task,
					cust_ext_instances.zonesn
					FROM
					cust_ext_instances
					Inner Join cust_inst_players ON cust_ext_instances.inst_id = cust_inst_players.inst_id
					$Where
					GROUP BY cust_ext_instances.inst_id";
					
					$connect = plugin::LoadMysql();
					$client->Message(15, "Loading expd zone instances...");
					$query_handle = $connect->prepare($query); $query_handle->execute();
					while(@row = $query_handle->fetchrow_array()){
						$client->Message(15, $row[9] . "(" . $row[0] . "): " . 
						quest::saylink("#show gotoexpdinst $row[15] $row[0]  $row[5]  $row[6]  $row[7]", 1, "$row[1]") 
						. " -  [" . 
						quest::saylink("#show jumpintoexp $row[0]", 1, "Join Expedition") . "]"
						);
					}
					
			
		}
		if($arg[1] eq "playerinst"){
			if($arg[2]){
				# MovePCInstance(zoneID, instanceID, x, y, z, heading)
				$query = "SELECT
					cust_expd_player_cache.id,
					cust_expd_player_cache.name,
					cust_expd_player_cache.level,
					cust_expd_player_cache.timelaston,
					instance_list.id,
					instance_list.zone,
					instance_list.`version`,
					zone.long_name,
					zone.safe_x,
					zone.safe_y,
					zone.safe_z
					FROM
					cust_expd_player_cache
					Inner Join instance_list_player ON cust_expd_player_cache.id = instance_list_player.charid
					Inner Join instance_list ON instance_list_player.id = instance_list.id
					Inner Join zone ON instance_list.zone = zone.zoneidnumber
					where instance_list.duration > 0
					and cust_expd_player_cache.name LIKE '%" . $arg[2] . "%'";
					$connect = plugin::LoadMysql();
					$client->Message(15, "Loading zone instances...");
					$query_handle = $connect->prepare($query); $query_handle->execute();
					while(@row = $query_handle->fetchrow_array()){
						$client->Message(15, $row[1] . "(" . $row[2] . "): " . quest::saylink("#show gotoinst $row[5] $row[4]  $row[8]  $row[9]  $row[10]", 1, "$row[7] ($row[6])($row[4])"));
					}
					
			}
		}
		if($arg[1] eq "currencies"){
			$query = "SELECT
				items.id,
				items.`Name`,
				items.stacksize,
				items.stackable,
				alternate_currency.id,
				alternate_currency.item_id
				FROM
				items
				INNER JOIN alternate_currency ON items.id = alternate_currency.item_id
				ORDER BY alternate_currency.id";
			$connect = plugin::LoadMysql();
			$client->Message(15, "Loading currencies...");
			$query_handle = $connect->prepare($query); $query_handle->execute();
			while(@row = $query_handle->fetchrow_array()){
				$Summon = "";
				$i = 1; $Summon .= quest::saylink("#summonitem " . $row[0] . " $i", 0, $i) . " ";
				$i = 100; $Summon .= quest::saylink("#summonitem " . $row[0] . " $i", 0, $i) . " ";
				$i = 250; $Summon .= quest::saylink("#summonitem " . $row[0] . " $i", 0, $i) . " ";
				$i = 1000; $Summon .= quest::saylink("#summonitem " . $row[0] . " $i", 0, $i) . " ";
				$client->Message(15, $row[4] . ": " . quest::varlink($row[0]) . " " . $row[0] . " Stacksize:" . $row[2] . " Summon: " . $Summon); 
			}
		}
		if($arg[1] eq "traps"){
			$query = "SELECT zone, x, y, z, id FROM `traps` WHERE `zone` = '". $zonesn ."'";
			$connect = plugin::LoadMysql();
			$client->Message(15, "Loading traps from zone...");
			$query_handle = $connect->prepare($query); $query_handle->execute();
			while(@row = $query_handle->fetchrow_array()){
				$client->Message(15, $row[4] . ": " . quest::saylink("#gmgoto " . int($row[1]) . " " . int($row[2]) . " " . int($row[3]), 0, $row[1] . ", " . $row[2] . ", " . $row[3]));
			}
		}
		if($arg[1] eq "zonelines"){
			$query = "SELECT zone, x, y, z, id FROM `zone_points` WHERE `zone` = '". $zonesn ."' and version = $instanceversion";
			$connect = plugin::LoadMysql();
			$client->Message(15, "Loading zonelines from zone...");
			$query_handle = $connect->prepare($query); $query_handle->execute();
			while(@row = $query_handle->fetchrow_array()){
				$client->Message(15, $row[4] . ": " . quest::saylink("#gmgoto " . int($row[1]) . " " . int($row[2]) . " " . int($row[3]), 0, $row[1] . ", " . $row[2] . ", " . $row[3]));
			}
		}
	}	
}