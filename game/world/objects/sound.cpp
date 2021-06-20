#include "sound.h"

#include "game/gamesession.h"
#include "world/world.h"

Sound::Sound() {
  }

Sound::Sound(World& world, Sound::Type type, const char* s, const Tempest::Vec3& pos, float range, bool freeSlot) {
  if(range<=0.f)
    range = 3500.f;

  auto& owner = *world.sound();
  if(!owner.isInListenerRange(pos,range))
    return;

  if(freeSlot) {
    std::lock_guard<std::mutex> guard(owner.sync);
    auto slot = owner.freeSlot.find(s);
    if(slot!=owner.freeSlot.end() && !slot->second->eff.isFinished())
      return;
    }

  SoundFx* snd = nullptr;
  if(type==T_Raw)
    snd = owner.game.loadSoundWavFx(s); else
    snd = owner.game.loadSoundFx(s);

  if(snd==nullptr)
    return;

  *this = owner.implAddSound(*snd, pos.x,pos.y,pos.z,range,WorldSound::maxDist);
  if(isEmpty())
    return;

  std::lock_guard<std::mutex> guard(owner.sync);
  owner.initSlot(*val);
  switch(type) {
    case T_Regular:
    case T_Raw: {
      if(freeSlot)
        owner.freeSlot[s] = val; else
        owner.effect.emplace_back(val);
      break;
      }
    case T_3D:{
      owner.effect3d.emplace_back(val);
      break;
      }
    }
  }

Sound::Sound(Sound&& other)
  :val(other.val){
  other.val = nullptr;
  }

Sound& Sound::operator =(Sound&& other) {
  std::swap(val,other.val);
  return *this;
  }

Sound::~Sound() {
  }

Sound::Sound(const std::shared_ptr<WorldSound::Effect>& val)
  :val(val) {
  }

Tempest::Vec3 Sound::position() const {
  return pos;
  }

bool Sound::isEmpty() const {
  return val==nullptr ? true : val->eff.isEmpty();
  }

bool Sound::isFinished() const {
  return val==nullptr ? true : val->eff.isFinished();
  }

void Sound::setOcclusion(float occ) {
  if(val!=nullptr)
    val->setOcclusion(occ);
  }

void Sound::setVolume(float v) {
  if(val!=nullptr)
    val->setVolume(v);
  }

void Sound::setMaxDistance(float v) {
  if(val!=nullptr)
    val->eff.setMaxDistance(v);
  }

void Sound::setRefDistance(float v) {
  if(val!=nullptr)
    val->eff.setRefDistance(v);
  }

void Sound::setPosition(const Tempest::Vec3& pos) {
  setPosition(pos.x,pos.y,pos.z);
  }

void Sound::setPosition(float x, float y, float z) {
  if(pos.x==x && pos.y==y && pos.z==z)
    return;
  if(val!=nullptr)
    val->eff.setPosition(x,y,z);
  pos = {x,y,z};
  }

void Sound::setLooping(bool l) {
  if(val!=nullptr)
    val->loop = l;
  }

void Sound::setAmbient(bool a) {
  if(val!=nullptr)
    val->ambient = a;
  }

void Sound::setActive(bool a) {
  if(val!=nullptr)
    val->active = a;
  }

void Sound::play() {
  if(val!=nullptr)
    val->eff.play();
  }
