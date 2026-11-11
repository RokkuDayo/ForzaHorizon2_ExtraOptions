EO Garage Engine Rev 1.0.3

INSTALLATION
Close Azimuth. Remove or disable the previous Garage Rev Test plugin first;
do not load GarageRevProbe.dll together with this replacement.
Extract the Garage Engine Rev folder into Azimuth/mods/Forza Horizon 2 and keep its entry
uncommented in mods/Forza Horizon 2/loadorder.txt. Restart the game.

FILES
Garage Engine Rev/plugins/EO_GarageEngineRev.dll
Garage Engine Rev/plugins/EO_GarageEngineRev.ini

USAGE
In the Engine Swap menu, hold RT to rev the preview engine. The native localized
Gas prompt appears at the end of the button list. Music drops to 5% of its previous level. The entire SFX mixer layer is
reduced directly to 5% of its previous gain, including ambient/crowd effects
routed to it. The car layer is untouched. Previous levels restore on exit.
The existing native preset fade timing is preserved. Festival and outpost
garages both temporarily release their own garage audio preset while revving.
The previous preset values restore when leaving Engine Swap.

LOGGING
Edit the INI beside the DLL:
[Logging]
Enabled=0

Set Enabled=1 to write EO_GarageEngineRev.log beside the DLL. Default is 0.
Restart the game after changing this setting. When disabled the plugin does
not create, overwrite or append its log. An existing log is left intact.
Extra Options still records its normal plugin loading result in its own log.
No Extra Options DLL, INI or save replacement is required.

COMPATIBILITY
Azimuth with Extra Options' mod-folder DLL loader. This packages the confirmed
working Engine Swap implementation; it does not add other garage menus.

1.0.2 UPDATE
Adds the outpost FLOW_DestinationHub preset to the scoped audio override.
Replace only the plugin DLL; keep your INI. This does not change engine swaps
or bank loading. Native fixture checks passed; outpost playback needs an
in-game confirmation.

1.0.3 RELEASE METADATA
Adds accurate Windows VERSIONINFO and reproducible build evidence. Runtime
machine code and imports are unchanged from 1.0.2. The DLL is still unsigned.
This is not a claim of a clean antivirus verdict. See ANTIVIRUS_REVIEW.md.
