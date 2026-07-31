#ifndef AZFORGE_CTRLS_API_H
#define AZFORGE_CTRLS_API_H

#include "WorldApi.h"

#ifdef __cplusplus

namespace Amod
{
    namespace Ctrls
    {
        class Ctrl : public World::Entity
        {
        public:
            Ctrl() = default;
            explicit Ctrl(const World::Entity& entity) : World::Entity(entity) {}

            bool                                                    IsDoor() const { return Kind() == AMOD_ENTITY_DOOR; }
        };

        /* Currently only doors (tag 2) are recognised. */
        inline bool                                                 IsCtrl(const World::Entity& entity)
        {
            return entity.IsValid() && entity.Kind() == AMOD_ENTITY_DOOR;
        }

        inline World::Entity                                        CurrentSpawn(int index)
        {
            AmodWorldEntity data;
            return World::GetSpawn(index, data) ? World::Entity(data) : World::Entity();
        }

        inline int                                                  Count()
        {
            const int entries = World::CurrentSpawnCount();
            int count = 0;

            for (int index = 0; index < entries; ++index)
            {
                count += IsCtrl(CurrentSpawn(index)) ? 1 : 0;
            }
            return count;
        }

        inline Ctrl                                                 At(int index)
        {
            const int entries = World::CurrentSpawnCount();
            int seen = 0;

            for (int at = 0; at < entries; ++at)
            {
                const World::Entity entry = CurrentSpawn(at);
                if (!IsCtrl(entry))
                {
                    continue;
                }
                if (seen == index)
                {
                    return Ctrl(entry);
                }
                ++seen;
            }
            return Ctrl();
        }

        inline int                                                  CountNamed(const char* name)
        {
            const int entries = World::CurrentSpawnCount();
            int count = 0;

            for (int index = 0; index < entries; ++index)
            {
                const World::Entity entry = CurrentSpawn(index);
                count += (IsCtrl(entry) && entry.Is(name)) ? 1 : 0;
            }
            return count;
        }

        inline Ctrl                                                 Named(const char* name, int index = 0)
        {
            const int entries = World::CurrentSpawnCount();
            int seen = 0;

            for (int at = 0; at < entries; ++at)
            {
                const World::Entity entry = CurrentSpawn(at);
                if (!IsCtrl(entry) || !entry.Is(name))
                {
                    continue;
                }
                if (seen == index)
                {
                    return Ctrl(entry);
                }
                ++seen;
            }
            return Ctrl();
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_CTRLS_API_H */
