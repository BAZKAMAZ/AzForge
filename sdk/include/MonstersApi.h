#ifndef AZFORGE_MONSTERS_API_H
#define AZFORGE_MONSTERS_API_H

#include "WorldApi.h"

#ifdef __cplusplus

namespace Amod
{
    namespace Monsters
    {
        class Monster : public World::Entity
        {
        public:
            Monster() = default;
            explicit Monster(const World::Entity& entity) : World::Entity(entity) {}
        };

        inline bool                                                 IsMonster(const World::Entity& entity)
        {
            return entity.IsValid() && entity.Kind() == AMOD_ENTITY_MONSTER;
        }

        inline int                                                  Count()
        {
            const int entities = World::CurrentEntityCount();
            int count = 0;

            for (int index = 0; index < entities; ++index)
            {
                count += IsMonster(World::CurrentEntity(index)) ? 1 : 0;
            }
            return count;
        }

        /* n-th monster in the current room; invalid if none. */
        inline Monster                                              At(int index)
        {
            const int entities = World::CurrentEntityCount();
            int seen = 0;

            for (int at = 0; at < entities; ++at)
            {
                const World::Entity entity = World::CurrentEntity(at);
                if (!IsMonster(entity))
                {
                    continue;
                }
                if (seen == index)
                {
                    return Monster(entity);
                }
                ++seen;
            }
            return Monster();
        }

        inline int                                                  CountNamed(const char* name)
        {
            const int entities = World::CurrentEntityCount();
            int count = 0;

            for (int index = 0; index < entities; ++index)
            {
                const World::Entity entity = World::CurrentEntity(index);
                count += (IsMonster(entity) && entity.Is(name)) ? 1 : 0;
            }
            return count;
        }

        /* n-th monster of this name; invalid if none. */
        inline Monster                                              Named(const char* name, int index = 0)
        {
            const int entities = World::CurrentEntityCount();
            int seen = 0;

            for (int at = 0; at < entities; ++at)
            {
                const World::Entity entity = World::CurrentEntity(at);
                if (!IsMonster(entity) || !entity.Is(name))
                {
                    continue;
                }
                if (seen == index)
                {
                    return Monster(entity);
                }
                ++seen;
            }
            return Monster();
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_MONSTERS_API_H */
