
sub encounterCommonMobs{ #get array of common mob ID's from zone longname
	my $zoneln = $_[0];
	my %zones = ("Kurn's Tower" => 0, "East Commonlands", 1);
	my @mobs = (
		[97004, 97003, 97011],
		[25405, 25349, 25401]
	);
	my $ref = @mobs[$zones{$zoneln}];
	@returnarray = @$ref;
	return @returnarray;
}

sub encounterRareMobs{ #get array of rare mob ID's from zone longname
	my $zoneln = $_[0];
	my %zones = ("Kurn's Tower" => 0, "East Commonlands", 1);
	my @raremobs = (
		[97064, 97019, 97016],
		[93084, 93088, 93124]
	);
	my $ref = @raremobs[$zones{$zoneln}];
	@returnarray = @$ref;
	return @returnarray;
}

sub encounterStrings{ #array of strings for enouncter text, order: message, sound, type
	my $zoneln = $_[0];
	my %zones = ("Kurn's Tower" => 0, "East Commonlands", 1);
	my @stringlist = ( 
		["The dead spring to life all around you!", "", "an undead ambush"],
		["You are ambushed!", "", "a dark elven ambush"]
	);
	my $ref = @stringlist[$zones{$zoneln}];
	@returnarray = @$ref;
	return @returnarray;
}

sub encounterReward{
	my $groupref = $_[0];
	my $winnerList = $_[1];
	my $encounterType = $_[2];
	my $zoneln = $_[3];
	my @group = @$groupref;
	
	$dbh = plugin::LoadMysql();
    if (!$dbh) {
   		quest::say("Failed to load MySQL... Tell Shin wtfm8! $winnerList");
   		return;
   	}
   	foreach $c (@group) {
		$sth = $dbh->prepare("UPDATE `account_custom` SET unclaimed_encounter_rewards = unclaimed_encounter_rewards + 1, unclaimed_encounter_rewards_total = unclaimed_encounter_rewards_total + 1 WHERE account_id = ?");
	   	$sth->execute($c->AccountID());	    	
	   }
	quest::we(13, "$winnerList successfully stopped $encounterType in $zoneln!");
}

sub AreaEncounter{
	#plugin::AreaEncounter($client, $zoneln, \@group, $triggerradius)
	my $npc = plugin::val('$npc');
	my $entity_list = plugin::val('$entity_list');
	quest::say("in plugin");
	my $c = $_[0];
	my $zoneln = $_[1];
	my $groupref = $_[2];
	my $triggerradius = $_[3];
	my @group = @$groupref;
	
	my @commonid = encounterCommonMobs($zoneln);
	my @rareid = encounterRareMobs($zoneln);
	my $isRare = 0;
	my $zindex = 0;
	my $mobamount = 0;
	my $npcID;
	
	my $random = int(rand(100));
	if($random <= 10) {
		$isRare = 1;
	}
	if($isRare){
		$npcID = $rareid[rand @rareid];
	}
	else{
		$npcID = $commonid[rand @commonid];
	}
		#determine mob level & mob count
		$level = $c->GetLevel();
		if ($isRare == 1) {
			$groupsize = scalar @group;
			$level += $groupsize;
			$mobamount = 1;
		}
		else{
			$groupsize = scalar @group;
			$mobamount += $groupsize;
		}
		
		foreach $member(@group){
			quest::say($member);
		}
		
		foreach my $i (1..$mobamount){
			my $ranx = $npc->GetX() + quest::ChooseRandom(-$triggerradius..$triggerradius);
			my $rany = $npc->GetY() + quest::ChooseRandom(-$triggerradius..$triggerradius);
			$newid = quest::encounterspawn($npcID, $level, $ranx, $rany, $c->GetZ(), $c->GetHeading());
			$newmob = $entity_list->GetMobID($newid);
			$newnpc = $newmob->CastToNPC();
			
			$newnpc->AddToHateList($c, 1);
			$enemies[$zindex] = $newmob->GetID();
			$zindex++;
		}
		return @enemies;#return enemy list for endcheck
}
return 1;