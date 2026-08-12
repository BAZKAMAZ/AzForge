# The particles workbench

One console command, `particles`, that drives every part of `ParticlesApi`. Load the mod, start a
level, open the console and type.

## Start here

```
particles firework
```

Six bursts around the player, then the word FIREWORK floating up through falling rain, shimmering
through a rainbow. Everything the API can do, in one go.

## Every command

| Type this | And you get |
| --- | --- |
| `particles` | the counts, same as `particles count` |
| `particles count` | how many of each kind are live, and the modes that are on |
| `particles paint 0 1 0` | everything in the air right now turns green |
| `particles tint 0 0 1` | keeps turning everything blue, including new particles |
| `particles tint off` | stops |
| `particles spawn` | 16 particles fountain up out of the player |
| `particles spawn 40 1` | 40 of them, into pool 1 |
| `particles spawn 40 1 textures/rooms/5_01_wall.jpg` | the same, with a texture you name |
| `particles text` | AZFORGE floats up over the player |
| `particles text HELLO 0.5` | your word, at half the size a score is drawn |
| `particles hold` | fire, sparks and trails freeze in the air |
| `particles hold off` | they carry on and fade as usual |
| `particles mark` | every new particle is marked; `tint` then paints only those |
| `particles mark off` | stops marking |
| `particles none` | the game cannot make a particle at all - everything goes still |
| `particles none off` | back to normal |
| `particles clear` | everything in the air ends now |
| `particles hide` | particles stop being drawn; the rest of the world is untouched |
| `particles show` | drawn again |
| `particles quiet` | the trail a bonus makes is refused |
| `particles loud` | allowed again |
| `particles firework` | the show |

## Things worth knowing before you decide something is broken

**`paint` on a fire looks like nothing happened.** It colours what is in the air at that instant,
and a fire is a stream of particles that live under a second - the ones you painted are gone before
you look up. Use `tint` for anything still being made, or `paint` after `hold`.

**`hold` fills the stores.** A held particle never finishes, so after a while the game runs out and
stops making new ones. That is it working. `hold off` releases them, `clear` sweeps up.

**Uni particles only exist while a bonus is being taken.** `particles count` will show zero of them
most of the time. Take a bonus and look again.

**Pool 2 is always empty.** The game never draws it. It is in the list because a mod that takes the
draw could use it.

**`spawn` needs a size you can see.** It fountains upward with gravity so you can watch it arc; if
you pass a texture path the game cannot open, it borrows the look of something already in the air.

**`text` scale is a multiple, not a size.** 1 is exactly the size the game floats a score at. 2 is
twice that. It is not a distance in the world.

## What the show demonstrates

`particles firework` is the one worth reading the source of. The letters of the word and the
raindrops are the same kind of particle, made a fraction of a second apart, and the game records no
owner on either. The word still shimmers and the rain does not, because the mod **marks** the
letters as they are born and repaints only what it marked.

That is the pattern for anything selective: set a birth handler for exactly as long as the thing
you care about is being created, mark what appears, and work on the marks afterwards.

## Using it from your own mod

```cpp
AmodParticleDesc desc = {};
desc.structSize = sizeof(desc);
desc.kind = AMOD_PARTICLE_KIND_POOL_1;
desc.textureA = Textures::Load("textures/rooms/5_01_wall.jpg");
desc.size = 0.03f;
desc.life = 1.0f;
desc.decay = 0.5f;
desc.speed = 0.2f;
desc.direction[1] = 1.0f;
desc.gravity[1] = -0.6f;
desc.colour[0] = 1.0f;
desc.colour[3] = 1.0f;

Particles::Spawn(desc);
```

Floating text goes through the game's own routine, so the letters are spaced the way the game
spaces them:

```cpp
float where[3] = {};
Camera::GetFocus(where);
Particles::SpawnText("HELLO", where);
```

A particle can also walk a list of textures instead of carrying one:

```cpp
const int frames[] = { a, b, c };
desc.animation = Particles::CreateAnimation(frames, 3);
desc.frameRate = 12.0f;
```

Set the alpha through `life`, not through `colour[3]` - the game writes the life into the alpha
every frame, so an alpha of your own never survives to be drawn.
