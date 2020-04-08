/*
 C-Dogs SDL
 A port of the legendary (and fun) action/arcade cdogs.
 Copyright (C) 1995 Ronny Wester
 Copyright (C) 2003 Jeremy Chin
 Copyright (C) 2003-2007 Lucas Martin-King

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 This file incorporates work covered by the following copyright and
 permission notice:

 Copyright (c) 2013-2019 Cong Xu
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */
#include "bullet_class.h"

#include <float.h>
#include <math.h>

#include "ai_utils.h"
#include "collision/collision.h"
#include "draw/drawtools.h"
#include "game_events.h"
#include "json_utils.h"
#include "log.h"
#include "net_util.h"
#include "objs.h"
#include "screen_shake.h"

BulletClasses gBulletClasses;

#define SPECIAL_LOCK 12
#define WALL_MARK_Z 5

// TODO: use map structure?
BulletClass* StrBulletClass(const char *s) {
	if (s == NULL || strlen(s) == 0) {
		return NULL;
	}
	CA_FOREACH(BulletClass, b, gBulletClasses.CustomClasses)
		if (strcmp(s, b->Name) == 0) {
			return b;
		}CA_FOREACH_END()
	CA_FOREACH(BulletClass, b, gBulletClasses.Classes)
		if (strcmp(s, b->Name) == 0) {
			return b;
		}CA_FOREACH_END()
	CASSERT(false, "cannot parse bullet name");
	return NULL;
}

// Draw functions

static void BulletDraw(GraphicsDevice *g, const int id,
		const struct vec2i pos) {
	const TMobileObject *obj = static_cast<const TMobileObject*>(CArrayGet(
			&gMobObjs, id));
	CASSERT(obj->isInUse, "Cannot draw non-existent mobobj");
	struct vec2i drawPos = svec2i_subtract(pos,
			svec2i(0, (int) (obj->z / Z_FACTOR)));

	CPicDrawContext c = CPicDrawContextNew();
	// Calculate direction based on velocity
	c.Dir = RadiansToDirection(svec2_angle(obj->thing.Vel) + MPI_2);
	const Pic *pic = CPicGetPic(&obj->thing.CPic, c.Dir);
	if (pic != NULL) {
		c.Offset = svec2i_scale_divide(pic->size, -2);
	}
	CPicDraw(g, &obj->thing.CPic, drawPos, &c);
}

static struct vec2 SeekTowards(const struct vec2 pos, const struct vec2 vel,
		const float speedMin, const struct vec2 targetPos,
		const int seekFactor) {
	// Compensate for bullet's velocity
	const struct vec2 targetVel = svec2_subtract(svec2_subtract(targetPos, pos),
			vel);
	// Don't seek if the coordinates are too big
	if (fabsf(targetVel.x) > 40 || fabsf(targetVel.y) > 40
			|| svec2_is_zero(targetVel)) {
		return vel;
	}
	const float targetMag = svec2_length(targetVel);
	const float magnitude = MAX(speedMin, svec2_length(vel));
	const float combinedX = vel.x / magnitude * seekFactor
			+ targetVel.x / targetMag;
	const float combinedY = vel.y / magnitude * seekFactor
			+ targetVel.y / targetMag;
	return svec2(combinedX * magnitude / (seekFactor + 1),
			combinedY * magnitude / (seekFactor + 1));
}

static void FireGuns(const TMobileObject *obj, const CArray *guns);
static void AddTrail(TMobileObject *obj, const int ticks);
typedef struct {
	HitType Type;
	struct vec2 Pos;
	struct vec2 Normal;
} HitResult;
static HitResult HitItem(TMobileObject *obj, const struct vec2 pos,
		const bool multipleHits);
bool BulletUpdate(struct MobileObject *obj, const int ticks) {
	ThingUpdate(&obj->thing, ticks);
	obj->count += ticks;
	obj->specialLock = MAX(0, obj->specialLock - ticks);
	if (obj->count < obj->bulletClass->Delay) {
		return true;
	}

	if (obj->range >= 0 && obj->count > obj->range) {
		if (!gCampaign.IsClient) {
			FireGuns(obj, &obj->bulletClass->OutOfRangeGuns);
		}
		if (obj->bulletClass->OutOfRangeSpark != NULL) {
			GameEvent s = GameEventNew(GAME_EVENT_ADD_PARTICLE);
			s.u.AddParticle.Class = obj->bulletClass->OutOfRangeSpark;
			s.u.AddParticle.Pos = obj->thing.Pos;
			s.u.AddParticle.Z = obj->z;
			GameEventsEnqueue(&gGameEvents, s);
		}
		return false;
	}

	const struct vec2 posStart = obj->thing.Pos;

	if (obj->bulletClass->SeekFactor > 0) {
		// Find the closest target to this bullet and steer towards it
		const TActor *owner = ActorGetByUID(obj->ActorUID);
		if (owner == NULL) {
			return false;
		}
		const TActor *target = AIGetClosestEnemy(posStart, owner, obj->flags);
		if (target && !target->dead) {
			for (int i = 0; i < ticks; i++) {
				obj->thing.Vel = SeekTowards(posStart, obj->thing.Vel,
						obj->bulletClass->SpeedLow, target->Pos,
						obj->bulletClass->SeekFactor);
			}
		}
	}

	HitResult hit = { HIT_NONE, svec2_zero(), svec2_zero() };
	if (!gCampaign.IsClient) {
		hit = HitItem(obj, posStart, obj->bulletClass->Persists);
	}
	struct vec2 pos = svec2_add(posStart,
			svec2_scale(obj->thing.Vel, (float) ticks));

	if (hit.Type != HIT_NONE) {
		GameEvent b = GameEventNew(GAME_EVENT_BULLET_BOUNCE);
		b.u.BulletBounce.UID = obj->UID;
		b.u.BulletBounce.HitType = (int) hit.Type;
		bool alive = true;
		if ((hit.Type == HIT_WALL && !obj->bulletClass->WallBounces)
				|| ((hit.Type == HIT_OBJECT || hit.Type == HIT_FLESH)
						&& obj->bulletClass->HitsObjects)) {
			b.u.BulletBounce.Spark = true;
			CASSERT(!gCampaign.IsClient, "Cannot process bounces as client");
			FireGuns(obj, &obj->bulletClass->HitGuns);
			if (hit.Type == HIT_WALL || !obj->bulletClass->Persists) {
				alive = false;
			}
			// Leave a wall mark if hitting a south-facing wall
			if (hit.Type == HIT_WALL && obj->thing.Vel.y < 0
					&& !TileIsOpaque(
							MapGetTile(&gMap,
									Vec2ToTile(
											svec2(hit.Pos.x,
													hit.Pos.y + 1))))) {
				b.u.BulletBounce.WallMark = true;
			}
		}
		const struct vec2 hitPos = hit.Type != HIT_NONE ? hit.Pos : pos;
		b.u.BulletBounce.Pos = b.u.BulletBounce.BouncePos = Vec2ToNet(hitPos);
		b.u.BulletBounce.Vel = Vec2ToNet(obj->thing.Vel);
		if (hit.Type == HIT_WALL && !svec2_is_zero(obj->thing.Vel) && alive) {
			// Bouncing
			GetWallBouncePosVel(posStart, obj->thing.Vel, hit.Pos, hit.Normal,
					&pos, &obj->thing.Vel);
			b.u.BulletBounce.Pos = Vec2ToNet(pos);
			b.u.BulletBounce.Vel = Vec2ToNet(obj->thing.Vel);
		}
		b.u.BulletBounce.HitSound = obj->thing.SoundLock == 0;
		if (obj->thing.SoundLock == 0) {
			obj->thing.SoundLock += SOUND_LOCK_THING;
		}
		GameEventsEnqueue(&gGameEvents, b);
		if (!alive) {
			return false;
		}
	}

	// Falling (grenades)
	if (obj->bulletClass->Falling.GravityFactor != 0) {
		bool hasDropped = obj->z <= 0;
		for (int i = 0; i < ticks; i++) {
			obj->z += obj->dz;
			if (obj->z <= 0) {
				obj->z = 0;
				if (obj->bulletClass->Falling.Bounces) {
					obj->dz = -obj->dz / 2;
				} else {
					obj->dz = 0;
				}
				if (!hasDropped) {
					if (!gCampaign.IsClient) {
						FireGuns(obj, &obj->bulletClass->Falling.DropGuns);
					}
				}
				hasDropped = true;
				if (obj->bulletClass->Falling.DestroyOnDrop) {
					return false;
				}
				SoundPlayAt(&gSoundDevice,
						StrSound(obj->bulletClass->HitSound.Wall), pos);
			} else {
				obj->dz -= obj->bulletClass->Falling.GravityFactor;
			}
			if (!obj->bulletClass->Falling.FallsDown) {
				obj->dz = MAX(0, obj->dz);
			}
		}
	}

	// Friction
	const bool isDiagonal = fabsf(obj->thing.Vel.x) < FLT_EPSILON
			&& fabsf(obj->thing.Vel.y) < FLT_EPSILON;
	const float frictionComponent =
			isDiagonal ?
					obj->bulletClass->Friction / sqrtf(2) :
					obj->bulletClass->Friction;
	for (int i = 0; i < ticks; i++) {
		if (obj->thing.Vel.x > FLT_EPSILON) {
			obj->thing.Vel.x -= frictionComponent;
		} else if (obj->thing.Vel.x < -FLT_EPSILON) {
			obj->thing.Vel.x += frictionComponent;
		}

		if (obj->thing.Vel.y > FLT_EPSILON) {
			obj->thing.Vel.y -= frictionComponent;
		} else if (obj->thing.Vel.y < -FLT_EPSILON) {
			obj->thing.Vel.y += frictionComponent;
		}
	}
	if (!MapTryMoveThing(&gMap, &obj->thing, pos)) {
		obj->count = obj->range;
		return false;
	}

	if (obj->bulletClass->Erratic) {
		for (int i = 0; i < ticks; i++) {
			obj->thing.Vel = svec2_add(obj->thing.Vel,
					svec2_scale(
							svec2((float) (rand() % 3) - 1,
									(float) (rand() % 3) - 1), 0.5f));
		}
	}

	// Proximity function, destroy
	// Only check proximity every now and then
	if (obj->bulletClass->ProximityGuns.size > 0 && !(obj->count & 3)) {
		if (!gCampaign.IsClient) {
			// Detonate the mine if there are characters in the tiles around it
			const struct vec2i tv = Vec2ToTile(pos);
			struct vec2i dv;
			for (dv.y = -1; dv.y <= 1; dv.y++) {
				for (dv.x = -1; dv.x <= 1; dv.x++) {
					const struct vec2i dtv = svec2i_add(tv, dv);
					if (!MapIsTileIn(&gMap, dtv)) {
						continue;
					}
					if (TileHasCharacter(MapGetTile(&gMap, dtv))) {
						FireGuns(obj, &obj->bulletClass->ProximityGuns);
						return false;
					}
				}
			}
		}
	}

	AddTrail(obj, ticks);

	return true;
}
static void FireGuns(const TMobileObject *obj, const CArray *guns) {
	const float angle = svec2_angle(obj->thing.Vel) + MPI_2;
	for (int i = 0; i < (int) guns->size; i++) {
		const WeaponClass **wc = static_cast<const WeaponClass**>(CArrayGet(
				guns, i));
		WeaponClassFire(*wc, obj->thing.Pos, obj->z, angle, obj->flags,
				obj->ActorUID, true, false);
	}
}
static void AddTrail(TMobileObject *obj, const int ticks) {
	const struct vec2 vel = svec2_subtract(obj->thing.Pos, obj->thing.LastPos);
	if (obj->bulletClass->Trail.P == NULL) {
		return;
	}
	AddParticle ap;
	memset(&ap, 0, sizeof ap);
	ap.Pos = svec2_scale(svec2_add(obj->thing.Pos, obj->thing.LastPos), 0.5f);
	ap.Z = obj->z / Z_FACTOR;
	ap.Angle = svec2_angle(vel) + MPI_2;
	ap.Mask = colorWhite;
	if (obj->bulletClass->Trail.P->Type == PARTICLE_PIC) {
		const CPic *cpic = &obj->bulletClass->Trail.P->u.Pic;
		const Pic *pic = CPicGetPic(cpic, 0);
		if (obj->bulletClass->Trail.Width > 0) {
			const struct vec2 trailSize = svec2(obj->bulletClass->Trail.Width,
					svec2_length(vel));
			ap.DrawScale = svec2_divide(trailSize,
					svec2_assign_vec2i(pic->size));
		}
	}
	if (obj->trail.ticksPerEmit > 0 && ticks > 0) {
		EmitterUpdate(&obj->trail, &ap, ticks);
	} else {
		EmitterStart(&obj->trail, &ap);
	}
}
typedef struct {
	enum HitType HitType;
	bool MultipleHits;
	TMobileObject *Obj;
	union {
		Thing *Target;
		struct vec2i TilePos;
	} u;
	struct vec2 ColPos;
	struct vec2 ColNormal;
	float ColPosDist2;
} HitItemData;
static bool HitItemFunc(Thing *ti, void *data, const struct vec2 colA,
		const struct vec2 colB, const struct vec2 normal);
static bool CheckWall(const struct vec2i tilePos);
static bool HitWallFunc(const struct vec2i tilePos, void *data,
		const struct vec2 col, const struct vec2 normal);
static void OnHit(HitItemData *data, Thing *target);
static HitResult HitItem(TMobileObject *obj, const struct vec2 pos,
		const bool multipleHits) {
	// Get all items that collide
	HitItemData data;
	data.HitType = HIT_NONE;
	data.MultipleHits = multipleHits;
	data.Obj = obj;
	data.ColPos = pos;
	data.ColNormal = svec2_zero();
	data.ColPosDist2 = -1;
	const CollisionParams params = {
	THING_CAN_BE_SHOT, COLLISIONTEAM_NONE, IsPVP(gCampaign.Entry.Mode) };
	OverlapThings(&obj->thing, pos, obj->thing.size, params, HitItemFunc, &data,
			CheckWall, HitWallFunc, &data);
	if (!multipleHits && data.ColPosDist2 >= 0) {
		if (data.HitType == HIT_OBJECT || data.HitType == HIT_FLESH) {
			OnHit(&data, data.u.Target);
		}
	}
	HitResult hit = { data.HitType, data.ColPos, data.ColNormal };
	return hit;
}
static HitType GetHitType(const Thing *ti, const TMobileObject *bullet,
		int *targetUID);
static void SetClosestCollision(HitItemData *data, const struct vec2 col,
		const struct vec2 normal, const HitType ht, Thing *target,
		const struct vec2i tilePos);
static bool HitItemFunc(Thing *ti, void *data, const struct vec2 colA,
		const struct vec2 colB, const struct vec2 normal) {
	UNUSED(colB);
	HitItemData *hData = static_cast<HitItemData*>(data);

	// Check bullet-to-other collisions
	if (!CanHit(hData->Obj->flags, hData->Obj->ActorUID, ti)) {
		goto bail;
	}

	// If we can hit multiple targets, just process those hits immediately
	// Otherwise, find the closest target and only process the hit for that one
	// at the end.
	if (hData->MultipleHits) {
		OnHit(hData, ti);
	} else {
		SetClosestCollision(hData, colA, normal,
				GetHitType(ti, hData->Obj, NULL), ti, svec2i_zero());
	}

	bail: return true;
}
static HitType GetHitType(const Thing *ti, const TMobileObject *bullet,
		int *targetUID) {
	int tUID = -1;
	HitType ht = HIT_NONE;
	switch (ti->kind) {
	case KIND_CHARACTER:
		ht = HIT_FLESH;
		tUID = ((const TActor*) CArrayGet(&gActors, ti->id))->uid;
		break;
	case KIND_OBJECT:
		ht = HIT_OBJECT;
		tUID = ((const TObject*) CArrayGet(&gObjs, ti->id))->uid;
		break;
	default:
		CASSERT(false, "cannot damage target kind")
		;
		break;
	}
	const TActor *bulletActor = ActorGetByUID(bullet->ActorUID);
	if (bullet->thing.SoundLock > 0
			|| !HasHitSound(bullet->flags,
					bulletActor ? bulletActor->PlayerUID : -1, ti->kind, tUID,
					bullet->bulletClass->Special, true)) {
		ht = HIT_NONE;
	}
	if (targetUID != NULL) {
		*targetUID = tUID;
	}
	return ht;
}
static bool CheckWall(const struct vec2i tilePos) {
	const Tile *t = MapGetTile(&gMap, tilePos);
	return t == NULL || TileIsShootable(t);
}
static bool HitWallFunc(const struct vec2i tilePos, void *data,
		const struct vec2 col, const struct vec2 normal) {
	HitItemData *hData = static_cast<HitItemData*>(data);

	SetClosestCollision(hData, col, normal, HIT_WALL, NULL, tilePos);

	return true;
}
static void SetClosestCollision(HitItemData *data, const struct vec2 col,
		const struct vec2 normal, const HitType ht, Thing *target,
		const struct vec2i tilePos) {
	// Choose the best collision point (i.e. closest to origin)
	const float d2 = svec2_distance_squared(col, data->Obj->thing.Pos);
	if (data->ColPosDist2 < 0 || d2 < data->ColPosDist2) {
		data->ColPos = col;
		data->ColPosDist2 = d2;
		data->ColNormal = normal;
		data->HitType = ht;
		if (ht == HIT_WALL) {
			data->u.TilePos = tilePos;
		} else {
			data->u.Target = target;
		}
	}
}
static void OnHit(HitItemData *data, Thing *target) {
	int targetUID = -1;
	data->HitType = GetHitType(target, data->Obj, &targetUID);
	const TActor *source = ActorGetByUID(data->Obj->ActorUID);
	Damage(data->Obj->thing.Vel, data->Obj->bulletClass->Power,
			data->Obj->bulletClass->Mass, data->Obj->flags, source,
			target->kind, targetUID, data->Obj->bulletClass->Special);
	if (data->Obj->thing.SoundLock <= 0) {
		data->Obj->thing.SoundLock += SOUND_LOCK_THING;
	}
	if (target->SoundLock <= 0) {
		target->SoundLock += SOUND_LOCK_THING;
	}
	if (data->Obj->specialLock <= 0) {
		data->Obj->specialLock += SPECIAL_LOCK;
	}
}

#define VERSION 3
static void LoadBullet(BulletClass *b, json_t *node,
		const BulletClass *defaultBullet, const int version);
void BulletInitialize(BulletClasses *bullets) {
	memset(bullets, 0, sizeof *bullets);
	CArrayInit(&bullets->Classes, sizeof(BulletClass));
	CArrayInit(&bullets->CustomClasses, sizeof(BulletClass));
}
static void BulletClassFree(BulletClass *b);
void BulletLoadJSON(BulletClasses *bullets, CArray *classes,
		json_t *bulletNode) {
	LOG(LM_MAP, LL_DEBUG, "loading bullets");
	int version;
	LoadInt(&version, bulletNode, "Version");
	if (version > VERSION || version <= 0) {
		CASSERT(false, "cannot read bullets file version");
		return;
	}

	// Defaults
	json_t *defaultNode = json_find_first_label(bulletNode, "DefaultBullet");
	if (defaultNode != NULL) {
		BulletClassFree(&bullets->Default);
		LoadBullet(&bullets->Default, defaultNode->child, NULL, version);
	}

	json_t *bulletsNode = json_find_first_label(bulletNode, "Bullets")->child;
	for (json_t *child = bulletsNode->child; child; child = child->next) {
		BulletClass b;
		LoadBullet(&b, child, &bullets->Default, version);
		CArrayPushBack(classes, &b);
	}

	bullets->root = bulletNode;
}
static void LoadParticle(const ParticleClass **p, json_t *node,
		const char *name);
static void LoadHitsound(char **hitsound, json_t *node, const char *name,
		const int version);
static void LoadBullet(BulletClass *b, json_t *node,
		const BulletClass *defaultBullet, const int version) {
	memset(b, 0, sizeof *b);
	if (defaultBullet != NULL) {
		memcpy(b, defaultBullet, sizeof *b);
		if (defaultBullet->Name != NULL) {
			b->Name =
					static_cast<char*>(malloc(strlen(defaultBullet->Name) + 1));
			if (b->Name == NULL && (strlen(defaultBullet->Name) + 1) > 0) {
				exit(1);
			}
			strcpy(b->Name, defaultBullet->Name);
//			CSTRDUP(b->Name, defaultBullet->Name);
		}
		if (defaultBullet->HitSound.Object != NULL) {
			b->HitSound.Object = static_cast<char*>(malloc(
					strlen(defaultBullet->HitSound.Object) + 1));
			if (b->HitSound.Object == NULL
					&& (strlen(defaultBullet->HitSound.Object) + 1) > 0) {
				exit(1);
			}
			strcpy(b->HitSound.Object, defaultBullet->HitSound.Object);
//			CSTRDUP(b->HitSound.Object, defaultBullet->HitSound.Object);
		}
		if (defaultBullet->HitSound.Flesh != NULL) {
			b->HitSound.Flesh = static_cast<char*>(malloc(
					strlen(defaultBullet->HitSound.Flesh) + 1));
			if (b->HitSound.Flesh == NULL
					&& (strlen(defaultBullet->HitSound.Flesh) + 1) > 0) {
				exit(1);
			}
			strcpy(b->HitSound.Flesh, defaultBullet->HitSound.Flesh);
//			CSTRDUP(b->HitSound.Flesh, defaultBullet->HitSound.Flesh);
		}
		if (defaultBullet->HitSound.Wall != NULL) {
			b->HitSound.Wall = static_cast<char*>(malloc(
					strlen(defaultBullet->HitSound.Wall) + 1));
			if (b->HitSound.Wall == NULL
					&& (strlen(defaultBullet->HitSound.Wall) + 1) > 0) {
				exit(1);
			}
			strcpy(b->HitSound.Wall, defaultBullet->HitSound.Wall);
//			CSTRDUP(b->HitSound.Wall, defaultBullet->HitSound.Wall);
		}
		// TODO: enable default bullet guns?
		memset(&b->Falling.DropGuns, 0, sizeof b->Falling.DropGuns);
		memset(&b->OutOfRangeGuns, 0, sizeof b->OutOfRangeGuns);
		memset(&b->HitGuns, 0, sizeof b->HitGuns);
		memset(&b->ProximityGuns, 0, sizeof b->ProximityGuns);
	}

	char *tmp;

	tmp = NULL;
	LoadStr(&tmp, node, "Name");
	if (tmp != NULL) {
		CFREE(b->Name);
		b->Name = tmp;
	}
	if (json_find_first_label(node, "Pic")) {
		CPicLoadJSON(&b->CPic, json_find_first_label(node, "Pic")->child);
	}
	if (json_find_first_label(node, "Trail")) {
		json_t *trail = json_find_first_label(node, "Trail")->child;
		tmp = NULL;
		LoadStr(&tmp, trail, "Particle");
		if (tmp != NULL) {
			b->Trail.P = StrParticleClass(&gParticleClasses, tmp);
			CFREE(tmp);
		}
		b->Trail.Width = 1.0f;
		LoadFloat(&b->Trail.Width, trail, "Width");
		LoadInt(&b->Trail.TicksPerEmit, trail, "TicksPerEmit");
	}
	LoadVec2i(&b->ShadowSize, node, "ShadowSize");
	LoadInt(&b->Delay, node, "Delay");
	if (json_find_first_label(node, "Speed")) {
		LoadFullInt(&b->SpeedLow, node, "Speed");
		b->SpeedHigh = b->SpeedLow;
	}
	LoadFullInt(&b->SpeedLow, node, "SpeedLow");
	LoadFullInt(&b->SpeedHigh, node, "SpeedHigh");
	b->SpeedLow = MIN(b->SpeedLow, b->SpeedHigh);
	b->SpeedHigh = MAX(b->SpeedLow, b->SpeedHigh);
	LoadBool(&b->SpeedScale, node, "SpeedScale");
	LoadFullInt(&b->Friction, node, "Friction");
	if (json_find_first_label(node, "Range")) {
		LoadInt(&b->RangeLow, node, "Range");
		b->RangeHigh = b->RangeLow;
	}
	LoadInt(&b->RangeLow, node, "RangeLow");
	LoadInt(&b->RangeHigh, node, "RangeHigh");
	b->RangeLow = MIN(b->RangeLow, b->RangeHigh);
	b->RangeHigh = MAX(b->RangeLow, b->RangeHigh);
	LoadInt(&b->Power, node, "Power");

	if (version < 2) {
		// Old version default mass = power
		b->Mass = (float) b->Power;
	} else {
		LoadFloat(&b->Mass, node, "Mass");
	}

	LoadVec2i(&b->Size, node, "Size");
	tmp = NULL;
	LoadStr(&tmp, node, "Special");
	if (tmp != NULL) {
		b->Special = StrSpecialDamage(tmp);
		CFREE(tmp);
	}
	LoadBool(&b->HurtAlways, node, "HurtAlways");
	LoadBool(&b->Persists, node, "Persists");
	LoadParticle(&b->Spark, node, "Spark");
	LoadParticle(&b->OutOfRangeSpark, node, "OutOfRangeSpark");
	LoadParticle(&b->WallMark, node, "WallMark");
	if (json_find_first_label(node, "HitSounds")) {
		json_t *hitSounds = json_find_first_label(node, "HitSounds")->child;
		LoadHitsound(&b->HitSound.Object, hitSounds, "Object", version);
		LoadHitsound(&b->HitSound.Flesh, hitSounds, "Flesh", version);
		LoadHitsound(&b->HitSound.Wall, hitSounds, "Wall", version);
	}
	LoadBool(&b->WallBounces, node, "WallBounces");
	LoadBool(&b->HitsObjects, node, "HitsObjects");
	if (json_find_first_label(node, "Falling")) {
		json_t *falling = json_find_first_label(node, "Falling")->child;
		LoadFloat(&b->Falling.GravityFactor, falling, "GravityFactor");
		LoadBool(&b->Falling.FallsDown, falling, "FallsDown");
		LoadBool(&b->Falling.DestroyOnDrop, falling, "DestroyOnDrop");
		LoadBool(&b->Falling.Bounces, falling, "Bounces");
	}
	LoadInt(&b->SeekFactor, node, "SeekFactor");
	LoadBool(&b->Erratic, node, "Erratic");

	b->node = node;

	LOG(LM_MAP, LL_DEBUG,
			"loaded bullet name(%s) shadowSize(%d, %d) delay(%d) speed(%f-%f)...",
			b->Name != NULL ? b->Name : "", b->ShadowSize.x, b->ShadowSize.y,
			b->Delay, b->SpeedLow, b->SpeedHigh);
	LOG(LM_MAP, LL_DEBUG,
			"...speedScale(%s) friction(%f) range(%d-%d) power(%d)...",
			b->SpeedScale ? "true" : "false", b->Friction, b->RangeLow,
			b->RangeHigh, b->Power);
	LOG(LM_MAP, LL_DEBUG,
			"...size(%d, %d) hurtAlways(%s) persists(%s) spark(%s, %s)...",
			b->Size.x, b->Size.y, b->HurtAlways ? "true" : "false",
			b->Persists ? "true" : "false",
			b->Spark != NULL ? b->Spark->Name : "",
			b->OutOfRangeSpark != NULL ? b->OutOfRangeSpark->Name : "");
	LOG(LM_MAP, LL_DEBUG, "...wallMark(%s)...",
			b->WallMark != NULL ? b->WallMark->Name : "");
	LOG(LM_MAP, LL_DEBUG,
			"...hitSounds(object(%s), flesh(%s), wall(%s)) wallBounces(%s)...",
			b->HitSound.Object != NULL ? b->HitSound.Object : "",
			b->HitSound.Flesh != NULL ? b->HitSound.Flesh : "",
			b->HitSound.Wall != NULL ? b->HitSound.Wall : "",
			b->WallBounces ? "true" : "false");
	LOG(LM_MAP, LL_DEBUG,
			"...hitsObjects(%s) gravity(%f) fallsDown(%s) destroyOnDrop(%s)...",
			b->HitsObjects ? "true" : "false", b->Falling.GravityFactor,
			b->Falling.FallsDown ? "true" : "false",
			b->Falling.DestroyOnDrop ? "true" : "false");
	LOG(LM_MAP, LL_DEBUG,
			"...dropGuns(%d) seekFactor(%d) erratic(%s) trail(%s@%f per %d)...",
			(int )b->Falling.DropGuns.size, b->SeekFactor,
			b->Erratic ? "true" : "false",
			b->Trail.P != NULL ? b->Trail.P->Name : "", b->Trail.Width,
			b->Trail.TicksPerEmit);
	LOG(LM_MAP, LL_DEBUG, "...outOfRangeGuns(%d) hitGuns(%d) proximityGuns(%d)",
			(int )b->OutOfRangeGuns.size, (int )b->HitGuns.size,
			(int )b->ProximityGuns.size);
}
static void LoadHitsound(char **hitsound, json_t *node, const char *name,
		const int version) {
	CFREE(*hitsound);
	*hitsound = NULL;
	LoadStr(hitsound, node, name);
	if (version < 3) {
		// Moved hit_XXX sounds to hits folder
		if (*hitsound != NULL) {
			char buf[CDOGS_FILENAME_MAX];
			strcpy(buf, "hits/");
			if (strncmp(*hitsound, "hit_", strlen("hit_")) == 0) {
				strcat(buf, *hitsound + strlen("hit_"));
				CFREE(*hitsound);
				*hitsound = static_cast<char*>(malloc(strlen(buf) + 1));
				if (*hitsound == NULL && (strlen(buf) + 1) > 0) {
					exit(1);
				}
				strcpy(*hitsound, buf);
//				CSTRDUP(*hitsound, buf);
			} else if (strncmp(*hitsound, "knife_", strlen("knife_")) == 0) {
				strcpy(buf, "hits/knife_");
				strcat(buf, *hitsound + strlen("knife_"));
				CFREE(*hitsound);
				*hitsound = static_cast<char*>(malloc(strlen(buf) + 1));
				if (*hitsound == NULL && (strlen(buf) + 1) > 0) {
					exit(1);
				}
				strcpy(*hitsound, buf);
//				CSTRDUP(*hitsound, buf);
			}
		}
	}
}
static void LoadParticle(const ParticleClass **p, json_t *node,
		const char *name) {
	char *tmp = NULL;
	LoadStr(&tmp, node, name);
	if (tmp != NULL) {
		*p = StrParticleClass(&gParticleClasses, tmp);
	}
	CFREE(tmp);
}
static void BulletClassesLoadWeapons(CArray *classes);
void BulletLoadWeapons(BulletClasses *bullets) {
	BulletClassesLoadWeapons(&bullets->Classes);
	BulletClassesLoadWeapons(&bullets->CustomClasses);
	json_free_value(&bullets->root);
}
static void BulletClassesLoadWeapons(CArray *classes) {
	for (int i = 0; i < (int) classes->size; i++) {
		BulletClass *b = static_cast<BulletClass*>(CArrayGet(classes, i));
		if (b->node == NULL) {
			continue;
		}

		if (json_find_first_label(b->node, "Falling")) {
			LoadBulletGuns(&b->Falling.DropGuns,
					json_find_first_label(b->node, "Falling")->child,
					"DropGuns");
		}
		LoadBulletGuns(&b->OutOfRangeGuns, b->node, "OutOfRangeGuns");
		LoadBulletGuns(&b->HitGuns, b->node, "HitGuns");
		LoadBulletGuns(&b->ProximityGuns, b->node, "ProximityGuns");

		b->node = NULL;
	}
}
void BulletTerminate(BulletClasses *bullets) {
	BulletClassFree(&bullets->Default);
	BulletClassesClear(&bullets->Classes);
	CArrayTerminate(&bullets->Classes);
	BulletClassesClear(&bullets->CustomClasses);
	CArrayTerminate(&bullets->CustomClasses);
}
void BulletClassesClear(CArray *classes) {
	for (int i = 0; i < (int) classes->size; i++) {
		BulletClassFree(static_cast<BulletClass*>(CArrayGet(classes, i)));
	}
	CArrayClear(classes);
}
static void BulletClassFree(BulletClass *b) {
	CFREE(b->Name);
	CFREE(b->HitSound.Object);
	CFREE(b->HitSound.Flesh);
	CFREE(b->HitSound.Wall);
	CArrayTerminate(&b->OutOfRangeGuns);
	CArrayTerminate(&b->HitGuns);
	CArrayTerminate(&b->Falling.DropGuns);
	CArrayTerminate(&b->ProximityGuns);
}

void BulletAdd(const NAddBullet add) {
	const struct vec2 pos = NetToVec2(add.MuzzlePos);

	// Find an empty slot in mobobj list
	TMobileObject *obj = NULL;
	int i;
	for (i = 0; i < (int) gMobObjs.size; i++) {
		TMobileObject *m = static_cast<TMobileObject*>(CArrayGet(&gMobObjs, i));
		if (!m->isInUse) {
			obj = m;
			break;
		}
	}
	if (obj == NULL) {
		TMobileObject m;
		memset(&m, 0, sizeof m);
		CArrayPushBack(&gMobObjs, &m);
		i = (int) gMobObjs.size - 1;
		obj = static_cast<TMobileObject*>(CArrayGet(&gMobObjs, i));
	}
	memset(obj, 0, sizeof *obj);
	obj->UID = add.UID;
	obj->bulletClass = StrBulletClass(add.BulletClass);
	ThingInit(&obj->thing, i, KIND_MOBILEOBJECT, obj->bulletClass->Size, 0);
	obj->z = (float) add.MuzzleHeight;
	obj->dz = (float) add.Elevation;

	EmitterInit(&obj->trail, obj->bulletClass->Trail.P, svec2_zero(), 0, 0, 0,
			0, 0, 0, obj->bulletClass->Trail.TicksPerEmit);

	obj->thing.Vel = svec2_scale(Vec2FromRadians(add.Angle),
			RAND_FLOAT(obj->bulletClass->SpeedLow,
					obj->bulletClass->SpeedHigh));
	if (obj->bulletClass->SpeedScale) {
		obj->thing.Vel.y *= (float) TILE_WIDTH / TILE_HEIGHT;
	}

	obj->ActorUID = add.ActorUID;
	obj->range = RAND_INT(obj->bulletClass->RangeLow,
			obj->bulletClass->RangeHigh);

	obj->flags = add.Flags;
	if (obj->bulletClass->HurtAlways) {
		obj->flags |= FLAGS_HURTALWAYS;
	}

	obj->isInUse = true;
	obj->thing.drawFunc = NULL;
	obj->thing.drawData.MobObjId = i;
	obj->thing.CPic = obj->bulletClass->CPic;
	obj->thing.CPicFunc = BulletDraw;
	obj->thing.ShadowSize = obj->bulletClass->ShadowSize;
	MapTryMoveThing(&gMap, &obj->thing, pos);
}

void BulletBounce(const NBulletBounce bb) {
	TMobileObject *o = MobObjGetByUID(bb.UID);
	if (o == NULL || !o->isInUse)
		return;
	const struct vec2 bouncePos = NetToVec2(bb.BouncePos);
	if (bb.HitSound) {
		PlayHitSound(&o->bulletClass->HitSound, (HitType) bb.HitType,
				bouncePos);
	}
	if (bb.Spark && o->bulletClass->Spark != NULL) {
		GameEvent s = GameEventNew(GAME_EVENT_ADD_PARTICLE);
		s.u.AddParticle.Class = o->bulletClass->Spark;
		s.u.AddParticle.Pos = bouncePos;
		s.u.AddParticle.Z = o->z;
		GameEventsEnqueue(&gGameEvents, s);
	}
	if (bb.WallMark && o->bulletClass->WallMark != NULL) {
		GameEvent s = GameEventNew(GAME_EVENT_ADD_PARTICLE);
		s.u.AddParticle.Class = o->bulletClass->WallMark;
		s.u.AddParticle.Pos = bouncePos;
		// Randomise Z on the wall
		s.u.AddParticle.Z = o->z + (int) RAND_FLOAT(-WALL_MARK_Z, WALL_MARK_Z);
		GameEventsEnqueue(&gGameEvents, s);
	}
	MapTryMoveThing(&gMap, &o->thing, NetToVec2(bb.Pos));
	o->thing.Vel = NetToVec2(bb.Vel);
}

void PlayHitSound(const HitSounds *h, const HitType t, const struct vec2 pos) {
	switch (t) {
	case HIT_NONE:
		// Do nothing
		break;
	case HIT_WALL:
		SoundPlayAt(&gSoundDevice, StrSound(h->Wall), pos);
		break;
	case HIT_OBJECT:
		SoundPlayAt(&gSoundDevice, StrSound(h->Object), pos);
		break;
	case HIT_FLESH:
		SoundPlayAt(&gSoundDevice, StrSound(h->Flesh), pos);
		break;
	default:
		CASSERT(false, "unknown hit type")
		break;
	}
}

void BulletDestroy(TMobileObject *obj) {
	CASSERT(obj->isInUse, "Destroying not-in-use bullet");
	AddTrail(obj, 0);
	MapRemoveThing(&gMap, &obj->thing);
	obj->isInUse = false;
}
