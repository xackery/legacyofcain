#######################################################
#::: Author:					Akkadius
#::: Created: 					1-1-2013
#::: Updated: 					4-18-2014
#::: FILE:						Expeditions.pl
#::: DESCRIPTION:				This plugin is one of the things required for the Perl Expedition Plugin
#::: EVENT Handlers: 			EVENT_SAY, EVENT_CLICKDOOR
#::: Required Perl Modules:		DBI (MySQL) connectivity
#::: Required Plugins:			plugin::LoadMysql()
#::: Wiki Page:					http://wiki.eqemulator.org/p?The_Expedition_and_Shared_Tasks_(Perl_Version)&frm=Main--Perl_Plugins_Master_Reference
######################################################

#::: String function for getting time left
sub GetTimeLeftAdv {
	my $InTime = $_[0];
	my $ReturnType = $_[1];
	my $CurTime = time;
	if ($InTime - $CurTime <= 0) { return "Unlimited"; }
	my $TimeDiff = $InTime - $CurTime;
	my $TimeDiffTotal = $TimeDiff;
	my $YearsLeft = 0;
	my $DaysLeft = 0;
	my $HoursLeft = 0;
	my $MinutesLeft = 0;
	my $SecondsLeft = 0;
	
	#::: Years
	if ($TimeDiff > 31556926){ $YearsLeft = int($TimeDiff / 31556926); $TimeDiff -= $YearsLeft * 31556926; }
	if ($TimeDiff > 86400) { $DaysLeft = int($TimeDiff / 86400); $TimeDiff -= $DaysLeft * 86400; }
	if ($TimeDiff > 3600) { $HoursLeft = int($TimeDiff / 3600); $TimeDiff -= $HoursLeft * 3600; }
	if ($TimeDiff > 60) { $MinutesLeft = int($TimeDiff / 60); $TimeDiff -= $MinutesLeft * 60; }
	$SecondsLeft = $TimeDiff;
	
	my $TimeLeft = 0;
	my $Days = "";
	if ($DaysLeft != 0) { $Days = $DaysLeft . "d "; }
	if (!$ReturnType) { $TimeLeft = $Days . $HoursLeft . "h " . $MinutesLeft . "m " . $SecondsLeft . "s "; }
	else {
		if ($ReturnType eq "S" || $ReturnType eq "s") { $TimeLeft = $TimeDiffTotal; }
		elsif ($ReturnType eq "M" || $ReturnType eq "m") { $TimeLeft = int($TimeDiffTotal / 60); }
		elsif ($ReturnType eq "H" || $ReturnType eq "h") { $TimeLeft = int($TimeDiffTotal / 3600); }
		elsif ($ReturnType eq "D" || $ReturnType eq "d") { $TimeLeft = int($TimeDiffTotal / 86400); }
		elsif ($ReturnType eq "Y" || $ReturnType eq "y") { $TimeLeft = int($TimeDiffTotal / 31556926); }
		else { $TimeLeft = 0; }
	}
	return $TimeLeft;
}

#::: Process called that will cleanup expired instances (Garbage Collection)
sub CheckForStaleInstances{
	$connect = plugin::LoadMysql();
	if(!$connect2){ $connect2 = plugin::LoadMysql(); }
	$query_handle = $connect->prepare("SELECT instance_list.id, cust_inst_players.inst_id, cust_ext_instances.identifier, cust_inst_players.player_name FROM cust_ext_instances INNER JOIN cust_inst_players ON cust_ext_instances.inst_id = cust_inst_players.inst_id LEFT JOIN instance_list ON cust_inst_players.inst_id = instance_list.id;"); $query_handle->execute();
	while (@row = $query_handle->fetchrow_array()){
		($StaleID, $ID2Purge) = ($row[0], $row[1]);
		if($StaleID eq "" && $ID2Purge > 0){
			$query_handle2 = $connect2->prepare("DELETE FROM `cust_inst_players` WHERE `inst_id` = '" . $ID2Purge . "';"); $query_handle2->execute();
			$query_handle2 = $connect2->prepare("DELETE FROM `cust_ext_instances` WHERE `inst_id` = '" . $ID2Purge . "';"); $query_handle2->execute();
		}
	}
}

#::: Function that calculates average level in either raid or group
sub CalculateReqs {
	$connect = plugin::LoadMysql();
	if($_[1] eq "group"){
		$query = "SELECT groupid FROM group_id WHERE `name` = '". $_[0] . "';";
		$query_handle = $connect->prepare($query); $query_handle->execute(); while (@row = $query_handle->fetchrow_array()){ $GPID = $row[0]; }
		$query = "SELECT CEILING(AVG(`cust_expd_player_cache`.level)), COUNT(*) as COUNT FROM `cust_expd_player_cache` Inner Join group_id ON `cust_expd_player_cache`.name = group_id.name WHERE group_id.groupid = ". $GPID . ";";
		$query_handle = $connect->prepare($query); $query_handle->execute(); while (@row = $query_handle->fetchrow_array()){ return @row; }
	}
	if($_[1] eq "raid"){
		$query = "SELECT raidid FROM raid_members WHERE `name` = '". $_[0] . "';";
		$query_handle = $connect->prepare($query); $query_handle->execute(); while (@row = $query_handle->fetchrow_array()){ $GPID = $row[0]; }
		$query = "SELECT CEILING(AVG(`cust_expd_player_cache`.level)), COUNT(*) AS COUNT FROM `cust_expd_player_cache` Inner Join raid_members ON `cust_expd_player_cache`.name = raid_members.name ". $GPID . ";";
		$query_handle = $connect->prepare($query); $query_handle->execute(); while (@row = $query_handle->fetchrow_array()){ return @row; }
	}
} 

#::: Returns player memberships, usually used on an expedition creation
sub CheckForPlayerMemberships {
	$connect = plugin::LoadMysql();
	my @Players;
	if($_[1] eq "group"){
		$query = "SELECT groupid FROM group_id WHERE `name` = '". $_[0] . "';";
		$query_handle = $connect->prepare($query); $query_handle->execute(); while (@row = $query_handle->fetchrow_array()){ $GPID = $row[0]; }
		$query = "SELECT `name` FROM `group_id` WHERE `groupid` = ". $GPID . ";";
		$query_handle = $connect->prepare($query); $query_handle->execute(); while (@row = $query_handle->fetchrow_array()){ push(@Players, $row[0]); }
	}
	if($_[1] eq "raid"){
		$query = "SELECT `raidid` FROM `raid_members` WHERE `name` = '". $_[0] . "';";
		$query_handle = $connect->prepare($query); $query_handle->execute(); while (@row = $query_handle->fetchrow_array()){ $GPID = $row[0]; }
		$query = "SELECT `name` FROM `raid_members` WHERE `raidid` = ". $GPID . ";";
		$query_handle = $connect->prepare($query); $query_handle->execute(); while (@row = $query_handle->fetchrow_array()){ push(@Players, $row[0]); }
	}
	return @Players;
}

#::: sub InitInstanceQueue(Instance Identifier - "Veeshan", Description of adventure size - "Group", (Number of Players), Task ID Association (Int), "Description of Adventure", duration - timer, version, zonesn, x, y, z, h, Required Average Level, 0 = No requirement, Minimum Number of players, 0 = None, IsSharedTask, Lockout Duration){
#::: Example usage: InitInstanceQueue("North Ro", "Group", 3, 130, "The Adventures of North Ro", 900, 0, "nro", 296, 3504.6, -24.5, 129, 92, 1, 1, 5000);
#::: This is the first step of this instance routine
#::: This routine will check for an adventure, it will see if the client has a pending invitation or if they are already invited
#::: If they have a pending invitation to the adventure they will be offered to accept
#::: If they are a member of the adventure they will be prompted to join the instance. Certain options will be available whether 
#::: the initiator is the leader of the instance or not.

sub InitInstanceQueue{
	$client = plugin::val('$client');
	$connect = plugin::LoadMysql();
	#::: Entity Variable "Purged" gets set when a client hasn't done a check for stales instances, this is for keeping tables clean
	my $PlayerCount = 1;
	my $AverageLevel = $client->GetLevel();
	CheckForStaleInstances();
	# $PlayerCount = $group->GroupCount();
	@ExpdPlayers = undef;
	@ExpdPlayers = $client->GetCleanName();
	if($client->GetGroup()){  @CalcReqs = CalculateReqs($client->GetCleanName(), "group"); $PlayerCount = $CalcReqs[1]; $AverageLevel = $CalcReqs[0]; @ExpdPlayers = CheckForPlayerMemberships($client->GetCleanName(), "group");  }
	$raid = $client->GetRaid(); if($raid){ @CalcReqs = CalculateReqs($client->GetCleanName(), "raid"); $PlayerCount = $CalcReqs[1]; $AverageLevel = $CalcReqs[0]; @ExpdPlayers = CheckForPlayerMemberships($client->GetCleanName(), "raid"); }
        $query = "SELECT
			cust_ext_instances.inst_id,
			cust_ext_instances.identifier,
			cust_ext_instances.type,
			cust_ext_instances.`limit`,
			cust_ext_instances.task_assoc,
			cust_ext_instances.inst_name,
			cust_inst_players.player_name,
			cust_inst_players.pending_invite,
			cust_inst_players.is_leader,
			instance_list.start_time,
			instance_list.duration
			FROM
			cust_ext_instances
			INNER JOIN cust_inst_players ON cust_ext_instances.inst_id = cust_inst_players.inst_id
			INNER JOIN instance_list ON cust_inst_players.inst_id = instance_list.id
			WHERE cust_ext_instances.identifier = '". $_[0] . "' AND 
			cust_ext_instances.type = '". $_[1] . "' AND
			cust_ext_instances.`limit` = '". $_[2] . "' AND
			cust_ext_instances.`task_assoc` = '". $_[3] . "' AND
			cust_ext_instances.`inst_name` = '". $_[4] . "' AND
			cust_inst_players.player_name = '". $client->GetCleanName() . "';"; 
		if($Debug) { $What = "#::: InitInstanceQueue #:::"; $client->Message(8, $What . " " . $query); quest::write("debug/instquery.txt", $What . "\n" . $query);}
		$query_handle = $connect->prepare($query); $query_handle->execute();
		$Init = 0;
		while (@row = $query_handle->fetchrow_array()){ ( $InstID, $Ident, $Type, $Limit, $Task_Assoc, $Inst_Name, $Player_N, $Pend_Invite, $Is_Leader, $start_time, $duration) = ($row[0], $row[1], $row[2], $row[3], $row[4], $row[5], $row[6], $row[7], $row[8], $row[9], $row[10]); $Init = 1; }
		if(($start_time + $duration) < time){ DestroyInstance($client->GetCleanName(), "force"); $Init = 0; }
		if($Init == 1){
			@IsLeader = CheckInstance($Ident, $client->GetCleanName());
			if($IsLeader[0] eq $Ident){
				if($IsLeader[8] == 1){ $client->Message(15, "You have a pending invitation for '" . $IsLeader[4] . "', would you like to [" . quest::saylink("Accept Instance" . $IsLeader[3], 1, "Accept Invitation") . "]?");}
				else{
					$client->SetEntityVariable("InstID", $IsLeader[3]);
					#$client->Message(15, "--------------------------------------------------------------------------");
					#$client->Message(15, "Current instance: (". $IsLeader[5] . " - " . $IsLeader[7] . ") - [ ". quest::saylink("Queue Instance" . $IsLeader[3], 1, $IsLeader[4]) . " ]");
					#if($IsLeader[2] == 1){ $client->Message(15, " ::[ " . quest::saylink("Destroy Instance", 1) . " ] "); }
					return;
				}
			}else{ $client->Message(15, "You do not belong to an expedition"); }
		}
	
	#::: Check for player Expedition membership in group/raid #:::
	$Players = "";
	my $qglobals = plugin::var('qglobals');	
	foreach $player (@ExpdPlayers){ if($qglobals->{"ExpdInfo_$player"}){ $Players .= $player . ", "; } }
	if($Players ne ""){
		$client->Message(15, "The following players are currently in an expedition and you cannot create another with them already in one: " . substr($Players, 0, -2));
		return;
	}
	#::: End Expedition Membership Checking #:::
	
	#::: Lockout Checking #:::
	$query = "SELECT `player`, `lockout_name`, `lockout_expire` FROM `cust_ext_lockouts`";
	if($Debug) { $What = "#::: CheckingLockouts #:::"; $client->Message(8, $What . " " . $query); quest::write("debug/instquery.txt", $What . "\n" . $query);}
	$query_handle = $connect->prepare($query); $query_handle->execute();
	$LP = " WHERE `player` = '' "; undef(@Lockouts);
	while (@row = $query_handle->fetchrow_array()){ 
		if($row[2] < time){ #::: The lockout has expired, let's purge it
			$LP .= " OR `player` = '". $row[0] . "' "; 
		}else{ $Lockouts{$row[0]} = [@row];  } #::: Lockout still persists, let's cache it to call it in the player checks
	}
	$query_handle2 = $connect2->prepare("DELETE FROM `cust_ext_lockouts` " . $LP); $query_handle2->execute();
	$PL = 0; 
	foreach $player (@ExpdPlayers){ 
		if($Lockouts{$player}[0] && $Lockouts{$player}[1] eq $_[4] && GetTimeLeftAdv($Lockouts{$player}[2]) ne "Unlimited"){
			$PL = 1;
			$client->Message(15, "Player '". $player . "' currently has a lockout for '". $Lockouts{$player}[1] . "' that expires in ". GetTimeLeftAdv($Lockouts{$player}[2]));
		}
	}
	if($PL == 1){ return; }
	#::: End Lockout Checking #:::
	
	if($PlayerCount > $_[2]){  $client->Message(15, "You have more players in your party than what is specified for this expedition : ". $_[1] . " of " . $_[2] . " players, you have " . $PlayerCount . " player(s) in your party"); return; }
	if($PlayerCount < $_[13]){ $client->Message(15, "You do not meet the requirements for this expedition: ". $_[1] . " of " . $_[13] . " players, you have " . $PlayerCount . " player(s) in your party"); }
	elsif($AverageLevel < $_[12]){ $client->Message(15, "You do not meet the requirements for this expedition: " . $_[12] . " average level, your average combined party level is ". $AverageLevel); }
	else{
		if($Init == 0){ $client->Message(15, "This entity is offering a expedition.");
			if($_[3] > 0){
				$query_handle = $connect->prepare("SELECT title, description, reward, startzone, minlevel, maxlevel, `repeatable` FROM `tasks` WHERE `id` = '" . $_[3] . "' LIMIT 1;"); $query_handle->execute();
				while (@row = $query_handle->fetchrow_array()){ ($TTitle, $TDesc) = ($row[0], $row[1]); }
				if($TDesc=~/\[1/i){ #::: There are multiple steps in the description
					$TDesc =~ s/\]//g; $TDesc =~ s/\[//g;
					@TDesc2 = split(',', $TDesc);
					foreach my $val (@TDesc2) {
						if($val=~ /^[+-]?\d+$/){}
						else{ $TDesc = substr($val, 0, -1);  last; }
					}
				}
				$TBody = "<c \"#FFFC17\">" . $TTitle . "</c><br><br> &nbsp;&nbsp;&nbsp;&nbsp; - " . $TDesc . "<br><br>";
			}else{ $TBody = ""; }
				
			$Br = plugin::PWBreak();
			$Qu = plugin::PWAutoCenter("Would you like to accept the following expedition?");
			$In = plugin::PWIndent();
			$Fg = "<c \"#00FF00\">";
			my $durv = "";
			if($_[5] < 3600){ $durv = "minutes"; } else{ $durv = "hours"; }
			quest::popup($_[4], "$Qu<br>$Br<br>$In$Fg$_[4]</c><br>" . $In . "Duration: ". ($_[5]/(60*60))%24 . ":" . ($_[5]/60)%60 . " $durv<br><br>$In$TBody", 600, 1);
			#::: InitInstance Variables are all stored here - 1st passing
			for($i=0;$i<=19;$i++){ $client->SetEntityVariable("InstRequest". $i, $_[$i]); if($Debug){ $client->Message(15, "Var " . $i . " " . $_[$i]); } }
		}
	}
}

#::: Initial instance initialize
sub DisplayInstanceQueue{
	my $client = plugin::val('$client');
	CheckForStaleInstances();
	$connect = plugin::LoadMysql();
	$query = "SELECT
		instance_list.zone,
		instance_list.version,
		instance_list.start_time,
		instance_list.duration,
		cust_ext_instances.identifier,
		cust_ext_instances.type,
		cust_ext_instances.`limit`,
		cust_ext_instances.task_assoc,
		cust_ext_instances.inst_name,
		cust_inst_players.player_name,
		cust_inst_players.pending_invite,
		cust_inst_players.is_leader,
		cust_ext_instances.inst_id
		FROM
		instance_list
		INNER JOIN cust_ext_instances ON instance_list.id = cust_ext_instances.inst_id
		INNER JOIN cust_inst_players ON cust_ext_instances.inst_id = cust_inst_players.inst_id
		WHERE cust_ext_instances.inst_id = ". $_[0] . "
		ORDER BY cust_inst_players.is_leader DESC, cust_inst_players.pending_invite;";
		if($Debug) { $What = "#::: DisplayInstanceQueue #:::"; $client->Message(8, $What . " " . $query); quest::write("debug/instquery.txt", $What . "\n" . $query);}
		$query_handle = $connect->prepare($query); $query_handle->execute();
		$n = 0; $Players = "";
		
	while (@row = $query_handle->fetchrow_array()){
		($Zone, $Version, $Start_Time, $Duration, $Ident, $Type, $Limit, $Task_Assoc, $inst_name, $player_name, $pending_invite, $is_leader, $inst_id) = ($row[0], $row[1], $row[2], $row[3], $row[4], $row[5], $row[6], $row[7], $row[8], $row[9], $row[10], $row[11], $row[12]); $n++;
		if($is_leader == 1){ $L = " - <c \"#80FF00\">Leader</c>"; } else{ $L = "" };
		if($pending_invite == 1){ $L = " - Pending"; } else{ $P = "" };
		$Players .= "" . $n . ") " . $player_name . $L . $P . "<br>";
	}
	if($Zone ne ""){
		if($Task_Assoc > 0){
			$query_handle = $connect->prepare("SELECT title, description, reward, startzone, minlevel, maxlevel, `repeatable` FROM `tasks` WHERE `id` = '" . $Task_Assoc . "' LIMIT 1;"); $query_handle->execute();
			while (@row = $query_handle->fetchrow_array()){ ($TTitle, $TDesc) = ($row[0], $row[1]); }
		}
		if($TDesc=~/\[1/i){ #::: There are multiple steps in the description
			$TDesc =~ s/\]//g; $TDesc =~ s/\[//g;
			@TDesc2 = split(',', $TDesc);
			foreach my $val (@TDesc2) {
				if($val=~ /^[+-]?\d+$/){}
				else{ $TDesc = substr($val, 0, -1);  last;}
			}
		}
		
		$TBody = "<c \"#FFFC17\">". $inst_name . "</c><br> <table><tr><td>
			<tr><td><c \"#00FF00\">Time Left</c></td><td>". plugin::GetTimeLeftAdv($Start_Time + $Duration) . "</td></tr>
			<tr><td><c \"#00FF00\">Members</c></td><td>". $n . "/". $Limit . " (". $Type . ")</td></tr>
			</td></tr></table>-------------------------------------<br>
			" . $Players . "<br>
			-------------------------------------<br>
			<c \"#FFFC17\">" . $TTitle . "</c><br>
			&nbsp;&nbsp;&nbsp;&nbsp; - " . $TDesc . "<br><br>
			<c \"#F07F00\">Click 'Yes' to Enter the Instance.</c>";
		quest::popup($inst_name, $TBody, 601, 1); 
		$client->SetEntityVariable("InstID", $inst_id);
	} else{ quest::popup("Expedition Expiration", "This expedition has expired...", 0, 0, 1); }
	#$client->Message(15, "DEBUG: INST ID IS $inst_id");
}

#::: Full on destroys instance for the leader
sub DestroyInstance{
	$connect = plugin::LoadMysql();
	$connect2 = plugin::LoadMysql();
	$query = "SELECT
		cust_inst_players.inst_id,
		cust_inst_players.player_name,
		cust_inst_players.is_leader,
		cust_ext_instances.task_assoc,
		cust_ext_instances.inst_name
		FROM
		cust_ext_instances
		INNER JOIN cust_inst_players ON cust_inst_players.inst_id = cust_ext_instances.inst_id
		INNER JOIN instance_list ON cust_ext_instances.inst_id = instance_list.id
		WHERE `cust_inst_players`.player_name = '" . $_[0] . "';";
		if($Debug) { $What = "#::: DESTROY INSTANCE #:::"; $client->Message(8, $What . " " . $query); quest::write("debug/instquery.txt", $What . "\n" . $query);}
		$query_handle = $connect->prepare($query); $query_handle->execute();
		while (@row = $query_handle->fetchrow_array()){
			if($_[0] eq $row[1] && $row[2] == 1 || $_[1] eq "force"){
				if($row[0] > 0){
					my $query2 = "SELECT
						cust_inst_players.inst_id,
						cust_inst_players.player_name,
						cust_inst_players.is_leader,
						cust_ext_instances.task_assoc,
						cust_ext_instances.inst_name
						FROM
						cust_ext_instances
						INNER JOIN cust_inst_players ON cust_inst_players.inst_id = cust_ext_instances.inst_id
						INNER JOIN instance_list ON cust_ext_instances.inst_id = instance_list.id
						WHERE `cust_inst_players`.inst_id = '" . $row[0] . "';";
						$query_handle2 = $connect2->prepare($query2); $query_handle2->execute();
						if($Debug) { $What = "#::: DESTROY INSTANCE SUB #:::"; $client->Message(8, $What . " " . $query2); quest::write("debug/instquery.txt", $What . "\n" . $query2);}
						while (@row2 = $query_handle2->fetchrow_array()){
							if($_[2] != 1){
								quest::crosszonesignalclientbyname($row2[1], 601); #::: Send Signal That Expedition has come to an end
								quest::crosszonesignalclientbyname($row2[1], $row2[3]); #::: Send Task Data so that $client->FailTask is Recieved on the other end
							}else{
								quest::crosszonesignalclientbyname($row2[1], 608); #::: Send Signal for Task Completion!
							}
						}
				}
				$query_handle2 = $connect2->prepare("DELETE FROM `cust_inst_players` WHERE `inst_id` = '". $row[0] . "';"); $query_handle2->execute(); 
				$query_handle2 = $connect2->prepare("DELETE FROM `cust_ext_instances` WHERE `inst_id` = '". $row[0] . "';"); $query_handle2->execute();
				# This was causing issues because players would try to zone to the same ID that was just used
				# $query_handle2 = $connect2->prepare("DELETE FROM `instance_list` WHERE `id` = '". $row[0] . "';"); $query_handle2->execute();
				$query_handle2 = $connect2->prepare("DELETE FROM `instance_list_player` WHERE `id` = '". $row[0] . "';"); $query_handle2->execute();
				if($_[2] != 1 && $client){ $client->FailTask($row[3]); $client->Message(15, "Your expedition has been destroyed - '" . $row[4] . "'"); }
				return 1;
			}else{ $client->Message(15, "You are not the leader of the expedition"); }
		}
}

#::: The database side of sending an instance invite, this might be deprecated in this current version
sub SendInstanceInvite{
	$CharName = $_[0]; $InstID = $_[1]; $CharID = $_[2];
	$query = "REPLACE INTO `cust_inst_players` (`inst_id`, `char_id`, `player_name`, `pending_invite`, `is_leader`, time_invited) VALUES (?, ?, ?, ?, ?, NOW());";
	$query_handle = $connect->prepare($query); $query_handle->execute($InstID, $CharID, $CharName, 1, 0); 
}

#::: Bool return for checking if a player has been invited or not
sub IsNotInvited{
	$query_handle = $connect->prepare("SELECT `inst_id`, `player_name` FROM `cust_inst_players` WHERE `inst_id` = ". $_[1] . " AND `player_name` = '". $_[0] . "';"); $query_handle->execute();
	while (@row = $query_handle->fetchrow_array()){ if($row[0] > 0){ return $row[0]; } }
	return 0;
}

sub CheckInstance{
	($Identifier, $CharName) = ($_[0], $_[1]);
	$query = "SELECT
		cust_ext_instances.identifier,
		cust_inst_players.player_name,
		cust_inst_players.is_leader,
		cust_inst_players.inst_id,
		cust_ext_instances.inst_name,
		cust_ext_instances.type,
		cust_ext_instances.task_assoc,
		cust_ext_instances.limit,
		cust_inst_players.pending_invite
		FROM
		cust_ext_instances
		INNER JOIN cust_inst_players ON cust_ext_instances.inst_id = cust_inst_players.inst_id
		INNER JOIN instance_list ON cust_inst_players.inst_id = instance_list.id
		WHERE cust_ext_instances.identifier = '". $Identifier . "'
		AND cust_inst_players.player_name = '". $CharName . "';";
		if($Debug) { $What = "#::: CheckInstance #:::"; $client->Message(8, $What . " " . $query); quest::write("debug/instquery.txt", $What . "\n" . $query); }
		$query_handle = $connect->prepare($query); $query_handle->execute();
		while (@row = $query_handle->fetchrow_array()){ if($row[0]){ return @row; } }
	return 0;
}

#::: Load Expedition info into a qglobal hash for fast callback
sub LoadExpeditionInfo2{
	my $qglobals = plugin::var('$qglobals');
	my $client = plugin::val('$client');
	$connect2 = plugin::LoadMysql();
	if(!$_[0] == 1){ #::: If set to 1, we want to re-read from the database and cache again
		if($qglobals{"ExpdInfo_" . $client->GetCleanName()}){ @ExpdInfo = split(',', $qglobals{"ExpdInfo_" . $client->GetCleanName()}); return @ExpdInfo; } #::: Caching Expedition Info for very fast responses
	}
	my $query = "SELECT
		cust_inst_players.inst_id,
		cust_inst_players.player_name,
		cust_inst_players.is_leader,
		cust_ext_instances.task_assoc,
		cust_ext_instances.inst_name,
		cust_ext_instances.avglevelreq,
		cust_ext_instances.ID,
		cust_ext_instances.shared_task,
		cust_ext_instances.duration,
		cust_ext_instances.expdate,
		cust_ext_instances.limit,
		cust_ext_instances.lockout,
		cust_ext_instances.return_zone,
		cust_ext_instances.return_version,
		cust_ext_instances.return_instanceid,
		cust_ext_instances.return_x,
		cust_ext_instances.return_y,
		cust_ext_instances.return_z,
		cust_ext_instances.zonesn,
		cust_ext_instances.boot_on_completion
		FROM
		cust_inst_players
		Inner Join cust_ext_instances ON cust_inst_players.inst_id = cust_ext_instances.inst_id
		WHERE cust_inst_players.player_name LIKE '%". $client->GetCleanName() . "%' LIMIT 1";
	if($Debug) { $What = "#::: LoadExpeditionInfo #:::"; $client->Message(8, $What . " " . $query); quest::write("debug/instquery.txt", $What . "\n" . $query);}
	my $query_handle = $connect2->prepare($query); $query_handle->execute(); my $NV = "";
	while (@row = $query_handle->fetchrow_array()){  foreach my $val (@row) { $NV .= "$val,"; } quest::setglobal("ExpdInfo_" . $client->GetCleanName(), substr ($NV, 0, -1), 7, "S" . ($row[9] - time)); return @row;  }
}

# plugin::InsertExpeditionCompletion($name, $AdvInfo[4], $AdvInfo[9]);
sub InsertExpeditionCompletion{
	$connect = plugin::LoadMysql();
	$query = "INSERT INTO `cust_expd_completion_records` (name, expedition, completed_secs, completed_mins, completion_time) VALUES (?, ?, ?, ?, NOW())";
	$comp_time = $_[3] - ($_[2] - time);
	$comp_time_mins = ($_[3] - ($_[2] - time)) / 60;
	$query_handle = $connect->prepare($query); $query_handle->execute($_[0], $_[1], $comp_time, $comp_time_mins);
}

#::: Dump Player Info, used for querying player info without touching the character_ table because of super slow queries. This keeps things fast and tracks online status
sub DumpPlayerCache{
	my $charid = plugin::val('$charid');
	my $name = plugin::val('$name');
	my $ulevel = plugin::val('$ulevel');
	#::: Cache Player Info for DB calls
	$connect = plugin::LoadMysql(); 
	$query = "REPLACE INTO `cust_expd_player_cache` (id, name, level, timelaston, online) VALUES (?, ?, ?, ?, ?);";
	$query_handle = $connect->prepare($query); $query_handle->execute($charid, $name, $ulevel, time(), $_[0]);
}