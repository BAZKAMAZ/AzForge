Everything in this folder is shipped inside the mod.

Lay it out the way the game asks for a file, without the files\ part - the build adds that:

    resmod\scripts\camera.txt      replaces scripts/camera.txt
    resmod\textures\ui\a60.png     replaces textures/ui/a60.png
    resmod\models\player.mmd       replaces models/player.mmd

The same keys data.pak uses: scripts, levels, textures, models, sounds, music.

A mod's own files go here too, not only replacements - a menu script of its own, a texture nothing
else uses. The loader looks here first, then on disk, then in data.pak.

The container is built from scratch every time, so deleting a file here stops it being shipped.

This readme is not: delete it whenever you like.
