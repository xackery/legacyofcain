sub EVENT_CONNECT{

	my $TextToCenter = plugin::PWAutoCenter("$indent Welcome to Legacy of Cain");
	my $TextToCenter2 = plugin::PWAutoCenter("Please note:");
	my $TextToCenter3 = plugin::PWAutoCenter(" We are under heavy development");
	my $TextToCenter4 = plugin::PWAutoCenter("Join us on Discord at: ");
	my $TextToCenter5 = plugin::PWAutoCenter("Github Source Changes");
	my $TextToCenter6 = plugin::PWAutoCenter("$link1");
	my $TextToCenter7 = plugin::PWAutoCenter("$link2");
	my $Indent = plugin::PWIndent();
	my $Link = plugin::PWHyperLink("https://discord.gg/GsS24sy", "Legacy of Cain Discord Channel!");
	my $Link2 = plugin::PWHyperLink("https://github.com/xackery/legacyofcain", "Legacy of Cain Github!");
	my $Red = plugin::PWColor("Red");
	quest::popup("Legacy of Cain", "$TextToCenter <br><br>
	$TextToCenter2 <br><br>
	$TextToCenter3 <br>
	<br><br>
	$Red $TextToCenter4 </c> <br>
	$Indent  $Indent $Link<br><br>
	$indent $Red $TextToCenter5 </c> <br>
	</c>
	 $Indent $Indent $Link2
	");

}