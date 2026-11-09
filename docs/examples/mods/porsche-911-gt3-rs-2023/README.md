# Disabled example

This is the previously exported Porsche data, accepted as format 1. It does
not include the car's media assets. Put those under `media/` before enabling.
Copy this folder to `mods/Forza Horizon 2/` and uncomment/add its folder
name in `mods/Forza Horizon 2/loadorder.txt` to enable it. Do not enable alongside another
addon or modified database that already inserts the same car IDs.

Place the car ZIP under media/cars/. The loader reads ZIP registration metadata automatically; zipmanifest.xml and a manifest car list are unnecessary.

The included strings.json adds the Porsche display name to Data_Car; English fallback supplies it in all languages.
