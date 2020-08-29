##############################################
# $Id: myUtilsTemplate.pm 7570 2015-01-14 18:31:44Z rudolfkoenig $
#
# Save this file as 99_myUtils.pm, and create your own functions in the new
# file. They are then available in every Perl expression.

package main;

use strict;
use warnings;
use POSIX;
use Data::Dumper;

sub
AudioHomeUtils_Initialize($$)
{
  my ($hash) = @_;
}

# Notes:
# 
# defmod = Define a device or modify it
# set im Code löst kein Event aus

sub AudioHome_init() {
	my $defauldIP = ReadingsVal('AudioHome_Manager','MQTT-IP','127.0.0.1:1883'); # get old or defauld
	my $maxAge = ReadingsNum('AudioHome_Manager','BT-MaxLifetime',10); # get old or defauld
	my $onChangeOverwrite = ReadingsNum('AudioHome_Manager','onChangeOverwrite','0'); # get old or defauld
	my $syncAfterChange = ReadingsNum('AudioHome_Manager','PlaySyncAfterChange',10); # get old or defauld

	## Manager
	fhem("defmod AudioHome_Manager dummy");
	fhem("attr AudioHome_Manager room AudioHome");
	fhem("attr AudioHome_Manager readingList DefineUser MQTT-IP Users Rooms LineIns LineOuts BT-MaxLifetime onChangeOverwrite PlaySyncAfterChange");
	fhem("attr AudioHome_Manager setList DefineUser MQTT-IP BT-MaxLifetime onChangeOverwrite PlaySyncAfterChange");
	fhem("attr AudioHome_Manager oldreadings PlaySyncAfterChange");
	fhem("attr AudioHome_Manager stateFormat OK");
	fhem("set AudioHome_Manager MQTT-IP $defauldIP");
	fhem("set AudioHome_Manager BT-MaxLifetime $maxAge");
	fhem("set AudioHome_Manager onChangeOverwrite $onChangeOverwrite");
	fhem("set AudioHome_Manager PlaySyncAfterChange $syncAfterChange");
	
	## ManagerNotify
	fhem('defmod AudioHome_ManagerLogic notify AudioHome_Manager:.* { AudioHome_ManagerLogic($EVTPART0, $EVTPART1);; }');
	fhem("attr AudioHome_ManagerLogic room AudioHome");

	## Line-In Notify
	fhem('defmod AudioHome_LineIn_Notify notify AudioHome_LineIn_.*:.* {AudioHome_LineInNotify($NAME, $EVTPART0, $EVTPART1);;}');
	fhem("attr AudioHome_LineIn_Notify room AudioHome");
	
	## User set Notify
	fhem('defmod AudioHome_setUserPlayNotify notify AudioHome_user_.*:plays:.* {
		my $user = $NAME;;
		$user =~ s/AudioHome_user_//ig;;
		AudioHome_setUserPlay($user, $EVTPART1);;
	}');
	fhem("attr AudioHome_setUserPlayNotify room AudioHome");
	
	## Line-Out At
	fhem('defmod AudioHome_LineOut_Contact at +*00:00:01 {AudioHome_Contact_LineOut()}');
	fhem("attr AudioHome_LineOut_Contact room AudioHome");

	## MQTT
	AudioHome_init_MQTT($defauldIP);
}

sub AudioHome_init_MQTT($) {
	my ($ip) = @_;
	
	## MQTT Client
	fhem("defmod AudioHome_MQTT_Client MQTT2_CLIENT $ip");
	fhem("attr AudioHome_MQTT_Client room AudioHome");
	
	## MQTT Listener
	fhem("defmod AudioHome_MQTT_Listener MQTT2_DEVICE");
	fhem("attr AudioHome_MQTT_Listener room AudioHome");
	fhem("attr AudioHome_MQTT_Listener IODev AudioHome_MQTT_Client");
	fhem('attr AudioHome_MQTT_Listener readingList */.*:.* { AudioHome_gotMQTT($TOPIC, $EVENT) }');
	fhem("attr AudioHome_MQTT_Listener stateFormat OK");
}

sub AudioHome_delete() {
	fhem("delete AudioHome_.*");
}

sub  AudioHome_ManagerLogic($$) {
	my ($variable, $value) = @_;
	
	
	if ($variable eq 'DefineUser:') {
		fhem('deletereading AudioHome_Manager DefineUser'); #löst kein event aus
		AudioHome_defineUser($value);
	}
	elsif ($variable eq 'MQTT-IP:') {
		print "set MQTT-IP to $value";
		AudioHome_init_MQTT($value);
	}
	elsif ($variable eq 'PlaySyncAfterChange:') {
		if ($value !~ /[0-9]{1,}/) {
			## reset
			my $oldValue = OldReadingsVal('AudioHome_Manager','PlaySyncAfterChange', 10);
			fhem("set AudioHome_Manager PlaySyncAfterChange $oldValue");
		}
		else {
			$value = int($value);
			if ($value < 1) {
				$value = 1;
			}
			elsif ($value > 60) {
				$value = 60;
			}
			fhem("set AudioHome_Manager PlaySyncAfterChange $value");
		}
	}
	
	## other non processed
}

sub AudioHome_LineInNotify($$$) {
	my ($NAME, $EVTPART0, $EVTPART1) = @_;
	my $LineInName = $NAME;
	$LineInName =~ s/AudioHome_LineIn_//ig; # cut AudioHome_LineIn_
	
	if ($EVTPART0 eq 'plays:') {
		my $room = ReadingsVal($NAME,'room', undef);
		if (defined($room)) {

			## for all Users
			my $usersStr = ReadingsVal('AudioHome_Manager','Users',undef);
			if (!defined($usersStr)) {
				return;
			}
			my @users = split(':', $usersStr);
			foreach ( @users ) {
				my $user = $_;

				if ($EVTPART1 eq 'play') {
					## start users play

					## User in Room?
					my $userRoom = ReadingsVal("AudioHome_user_$user",'currentRoom','none');
					if ($userRoom eq $room) {
						fhem("set AudioHome_user_$user plays $LineInName");
						AudioHome_setRoomPlayUser($room, $user);
						print "\nUser play: $user\n"
					}
				}
				elsif ($EVTPART1 eq 'none') {
					## stop users play
					
					## User plays this Device
					my $plays = ReadingsVal("AudioHome_user_$user",'plays','none');
					if ($plays eq $LineInName) {
						AudioHome_UserPlaysNone($user);
					}
				}
			}
		}
	}
	elsif ($EVTPART0 eq 'Address:') {
		## Device has startet, transmit last state
		
		my $lastState = ReadingsVal($NAME,'plays','none');
		fhem("set AudioHome_MQTT_Client publish AudioDevice/Line-In/$LineInName/plays $lastState");
	}
}

sub AudioHome_Contact_LineOut() {
	## get LineOuts
	my $lineOutsStr = ReadingsVal('AudioHome_Manager','LineOuts',undef);
	if (!defined($lineOutsStr)) {
		return;
	}
	my @lineOuts = split(':', $lineOutsStr);

	## for all LineOuts
	foreach ( @lineOuts ) {
		my $lineOut = $_;
		
		## go to room
		my $room = ReadingsVal("AudioHome_LineOut_$lineOut",'room',undef);
		if (defined($room)) {
			
			## go to User who plays
			my $user = ReadingsVal("AudioHome_room_$room",'playUser','none');
			if ($user ne 'none') {
				
				## get LineIn
				my $plays = ReadingsVal("AudioHome_user_$user",'plays','none');
				if ($plays ne 'none') {
					my $address = ReadingsVal("AudioHome_LineIn_$plays",'Address','none');
					fhem("set AudioHome_MQTT_Client publish AudioDevice/Line-Out/$lineOut $address");
					next;
				}
			}
		}
		
		## no User plays
		fhem("set AudioHome_MQTT_Client publish AudioDevice/Line-Out/$lineOut none");
	}
}

sub AudioHome_getPlaysAfterChange() {
	my $time = ReadingsNum('AudioHome_Manager','PlaySyncAfterChange', 10);
	if ($time >= 60) {
		return '00:00:10';
	}
	return sprintf("00:00:%02d", $time);
}

sub AudioHome_defineRoom($) {
	my ($str) = @_;
	my $name = "AudioHome_room_" . $str;
	
	## get old Values
	my $readingList = AttrVal($name,'readingList','playUser');
	
	## define
	fhem("defmod $name dummy");
	fhem("attr $name room AudioHome");
	fhem("attr $name readingList $readingList");
	fhem("attr $name stateFormat spiele Nutzer: playUser");
	fhem("set $name playUser none");
	
	## add to Manager
	my $rooms = ReadingsVal('AudioHome_Manager','Rooms','');
	if ($rooms eq '') {
		$rooms = $str;
	}
	else {
		$rooms .= ':'.$str; 
	}
	fhem("set AudioHome_Manager Rooms $rooms");
}

sub AudioHome_defineUser($) {
	my ($str) = @_;
	my $name = "AudioHome_user_" . $str;
	
	fhem("defmod $name dummy");
	fhem("attr $name room AudioHome");
	fhem("attr $name readingList BT-MAC currentRoom plays");
	fhem("attr $name oldreadings plays");
	AudioHome_updateUserSetList($str);
	fhem("attr $name webCmd plays");
	fhem("attr $name webCmdLabel Plays");
	fhem("attr $name stateFormat {return AudioHome_UserState('$name');;}");
	
	## add to Manager
	my $users = ReadingsVal('AudioHome_Manager','Users','');
	if ($users eq '') {
		$users = $str;
	}
	else {
		$users .= ':'.$str; 
	}
	fhem("set AudioHome_Manager Users $users");
}

sub AudioHome_updateUserSetList($) {
	my ($user) = @_;
	my $LineIns = ReadingsVal('AudioHome_Manager','LineIns','');
	
	$LineIns =~ s/:/,/ig;
	
	fhem("attr AudioHome_user_$user setList BT-MAC plays:select,none,$LineIns");
}

sub AudioHome_defineLineIn($) {
	my ($str) = @_;
	my $name = "AudioHome_LineIn_" . $str;
	
	fhem("defmod $name MQTT2_DEVICE");
	fhem("attr $name room AudioHome");
	fhem("attr $name IODev AudioHome_MQTT_Client");
	fhem("attr $name readingList AudioDevice/Line-In/$str/Address:.* Address\nAudioDevice/Line-In/$str/plays:.* plays\nAudioDevice/Line-In/$str/log:.* log");
	fhem("attr $name stateFormat {return AudioHome_LineIn('$name');;}");
	
	## add to Manager
	my $lineIns = ReadingsVal('AudioHome_Manager','LineIns','');
	if ($lineIns eq '') {
		$lineIns = $str;
	}
	else {
		$lineIns .= ':'.$str; 
	}
	fhem("set AudioHome_Manager LineIns $lineIns");
	
	## Update User setList
	my $usersStr = ReadingsVal('AudioHome_Manager','Users',undef);
	my @users = split(':', $usersStr);

	foreach ( @users ) {
		my $currentUser = $_;
		AudioHome_updateUserSetList($currentUser);
	}
	
	#room muss mit "setreading <devspec> <reading> <value>" gesetzt werden
}

sub AudioHome_defineLineOut($) {
	my ($str) = @_;
	my $name = "AudioHome_LineOut_" . $str;
	
	## define
	fhem("defmod $name dummy");
	fhem("attr $name room AudioHome");
	fhem("attr $name readingList room plays");
	fhem("attr $name setList room");
	fhem("attr $name stateFormat {return AudioHome_LineOut('$name');;}");
	
	## add to Manager
	my $lineOuts = ReadingsVal('AudioHome_Manager','LineOuts','');
	if ($lineOuts eq '') {
		$lineOuts = $str;
	}
	else {
		$lineOuts .= ':'.$str; 
	}
	fhem("set AudioHome_Manager LineOuts $lineOuts");
}

sub AudioHome_gotMQTT($$) {
	## Variablen
	my ($topic, $value) = @_;
	my @topicParts = split('/', $topic);
	my $task = $topicParts[0];
	#print $topic."\n";
	
	if ($task eq 'Lokalisierung') {
		## Lokalisierung
		my $rssi = $value;
		my $room = $topicParts[1];
		my $mac = $topicParts[2];
		#print Dumper(@topicParts, $rssi);

		if (!defined($room) || !defined($mac)) {
			return;
		}

		my $roomDevice = "AudioHome_room_" . $room;

		## room exist?
		my $roomExist = InternalVal($roomDevice,'NAME',undef);
		if (!defined($roomExist)) {
			AudioHome_defineRoom($room);
		}

		## set Value
		AudioHome_setRSSI($roomDevice, $mac, $rssi);
	}
	elsif ($task eq 'AudioDevice') {
		## AudioDevice
		my $line = $topicParts[1];
		my $name = $topicParts[2];
		if($line eq 'Line-In') {
			## Line-In
			my $variableName = $topicParts[3];
			
			## Device exist?
			my $deviceExist = InternalVal("AudioHome_LineIn_$name",'NAME',undef);
			if (!defined($deviceExist)) {
				AudioHome_defineLineIn($name);
			}
			
			## value will be set automaticly (Type: MQTT2_DEVICE)
		}
		elsif ($line eq 'Line-Out') {
			## Line-out
			
			## Device exist?
			my $deviceExist = InternalVal("AudioHome_LineOut_$name",'NAME',undef);
			if (!defined($deviceExist)) {
				AudioHome_defineLineOut($name);
			}
			
			## set Value
			fhem("set AudioHome_LineOut_$name plays $value");
		}
	}
}

sub AudioHome_setRSSI($$$) {
	my ($roomDevice, $mac, $rssi) = @_;
	
	## reading exist?
	my $readingList = AttrVal($roomDevice,'readingList','');
	my @readingListArray = split(' ', $readingList);
	my %readingListHash = map { $_ => 1 } @readingListArray;
	if(!exists($readingListHash{$mac})) { 
		fhem("attr $roomDevice readingList $readingList $mac"); # extends readingList by mac
	}
	
	## set
	fhem("set $roomDevice $mac $rssi"); # triffert kein Notify
	updateMac($mac);
}

sub updateMac($) {
	my ($mac) = @_;
	$mac =~ s/:/_/ig; # s/FIND/REPLACE/ + Flags: i = case-insensitice, g = all matches
	
	## get User
	my $user = getUserFromMac($mac);
	if (!defined($user)) {
		return;
	}
	
	## get Rooms
	my %valuesPairs;
	my $roomsStr = ReadingsVal('AudioHome_Manager','Rooms',undef);
	if (!defined($roomsStr)) {
		return;
	}
	my @rooms = split(':', $roomsStr);
	
	## get Values
	foreach ( @rooms ) {
		my $room = $_;
		my $rssi = ReadingsVal("AudioHome_room_$room",$mac,undef);
		if (defined($rssi)) {
			## to old?
			my $age = ReadingsAge("AudioHome_room_$room",$mac,'inf');
			my $maxAge = ReadingsVal('AudioHome_Manager','BT-MaxLifetime',10);
			if ($age < $maxAge) {
				## add
				$valuesPairs{$room} = $rssi;
			}
		}
	}
	
	## Auswertung
	my $bestRoom = 'none';
	my $bestRssi = '-inf';
	foreach my $room (keys %valuesPairs) {
		if ($valuesPairs{$room} > $bestRssi) { # > weil rssi negativ ist und je näher bei 0 desto besser
			$bestRssi = $valuesPairs{$room};
			$bestRoom = $room;
		}
	}
	
	## set Room
	AudioHome_userChangeRoom($user, $bestRoom)
}

sub getUserFromMac($) {
	my ($mac) = @_;
	$mac =~ s/:/_/ig; # s/FIND/REPLACE/ + Flags: i = case-insensitice, g = all matches
	
	## get all Users
	my $usersStr = ReadingsVal('AudioHome_Manager','Users',undef);
	if (!defined($usersStr)) {
		return undef;
	}
	my @users = split(':', $usersStr);
	
	## search User
	foreach ( @users ) {
		my $currentUser = $_;
		my $currentMac = ReadingsVal("AudioHome_user_$currentUser",'BT-MAC',undef);
		$currentMac =~ s/:/_/ig;
		if (defined($currentMac) && $mac eq $currentMac) {
			return $currentUser;
		}
	}
	
	## kein User gefunden
	#print "nop\n";
	return undef;
}

sub AudioHome_userChangeRoom($$) {
	my ($user, $room) = @_;
	
	#room changed?
	my $oldRoom = ReadingsVal("AudioHome_user_$user",'currentRoom','none');
	if ($room ne $oldRoom && $oldRoom ne 'none') {
		my $offIn = AudioHome_getPlaysAfterChange();
		fhem("defmod AudioHome_playAfter_$oldRoom at +$offIn set AudioHome_room_$oldRoom playUser none");
		print "erzeuge AudioHome_playAfter_$oldRoom \n";
		fhem("attr AudioHome_playAfter_$oldRoom room AudioHome");
	}

	## set room
	fhem("set AudioHome_user_$user currentRoom $room");
	
	## if I play => notify room
	my $plays = ReadingsVal("AudioHome_user_$user",'plays','none');
	if ($plays ne 'none') {
		AudioHome_setRoomPlayUser($room, $user)
	}
}

sub AudioHome_setRoomPlayUser($$) {
	my ($room, $user) = @_;
	my $overwrite = ReadingsVal('AudioHome_Manager','onChangeOverwrite','0');
	my $playUser = ReadingsVal("AudioHome_room_$room",'playUser','none');
	
	if ($playUser eq 'none' || $playUser eq $user || $overwrite eq '1') { # Keiner oder ich selbst spielt; oder Overwrite
		fhem("set AudioHome_room_$room playUser $user");
		
		## delete playAfter-Device
		my $playAfterDevice = InternalVal("AudioHome_playAfter_$room",'NAME',undef);
		
		if (defined($playAfterDevice)) {
			fhem("delete AudioHome_playAfter_$room");
		}
		
		## send MQTT to LineOutDevices
		#AudioHome_sendMQTTforLineOut($room, $user); # Entkoppelt: auto jede Sekunde für alle Devices
	}
}

sub AudioHome_UserPlaysNone($) {
	my ($user) = @_;
	
	fhem("set AudioHome_user_$user plays none");
	
	## inform rooms
	my $roomsStr = ReadingsVal('AudioHome_Manager','Rooms',undef);
	if (!defined($roomsStr)) {
		return;
	}
	my @rooms = split(':', $roomsStr);
	
	## get Values
	foreach ( @rooms ) {
		my $room = $_;
		my $playUser = ReadingsVal("AudioHome_room_$room",'playUser','none');
		if ($user eq $playUser) {
			fhem("set AudioHome_room_$room playUser none");
		}
	}
}

sub AudioHome_setUserPlay($$) {
	my ($user, $LineInName) = @_;
	
	my $oldValue = OldReadingsVal("AudioHome_user_$user",'plays', 'none');
	print "\nUser: $user\nLine: $LineInName\nold: $oldValue\n";
	
	## alte Widergabe stoppen
	if ($oldValue ne $LineInName && $oldValue ne 'none') {
		## get all Users
		my $usersStr = ReadingsVal('AudioHome_Manager','Users',undef);
		my @users = split(':', $usersStr);

		## is one User still playing?
		my $onePlays = 0;
		foreach ( @users ) {
			my $currentUser = $_;
			my $currentUserPlays = ReadingsVal("AudioHome_user_$currentUser",'plays', 'none');
			if ($currentUserPlays eq $oldValue) {
				$onePlays = 1;
				last; #break
			}
		}
		
		## stoppe Wiedergabe
		if (!$onePlays) {
			fhem("set AudioHome_MQTT_Client publish AudioDevice/Line-In/$oldValue/plays none");
		}
	}
	
	## Stop?
	if ($LineInName eq 'none') {
		AudioHome_UserPlaysNone($user);
		return;
	}
	
	## Neue Wiedergabe
	my $userRoom = ReadingsVal("AudioHome_user_$user",'currentRoom','none');
	
	if ($userRoom ne 'none') {
		## LineIn wiedergabe starten, wenn noch nicht spielt
		my $LinePlays = ReadingsVal("AudioHome_LineIn_$LineInName",'plays', 'none');
		if ($LinePlays ne 'play') {
			fhem("set AudioHome_MQTT_Client publish AudioDevice/Line-In/$LineInName/plays play");
		}
		
		## Raum wiedergabe starten
		AudioHome_setRoomPlayUser($userRoom, $user);
	}
}

sub AudioHome_UserState($) {
	my ($device) = @_;
	my $mac = ReadingsVal($device,'BT-MAC', '');
	
	if ($mac eq '') {
		return "Keine BT-MAC konfiguriert";
	}
	
	my $room = ReadingsVal($device,'currentRoom', '');
	return "Aufenthaltsraum: $room";
}

sub AudioHome_LineIn($) {
	my ($device) = @_;
	my $mac = ReadingsVal($device,'room', '');
	
	if ($mac eq '') {
		return "Keine Raum zugewiesen, nutze Befehl: <br/> setreading $device room RAUMNAME";
	}
	return '';
}

sub AudioHome_LineOut($) {
	my ($device) = @_;
	my $mac = ReadingsVal($device,'room', '');
	
	if ($mac eq '') {
		return "Keine Raum zugewiesen, nutze Befehl: <br/> set $device room RAUMNAME";
	}
	return '';
}

1;
