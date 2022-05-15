#pragma once

#include <Tempest/Signal>

#include "objectsbucket.h"

class SceneGlobals;
class AnimMesh;
class Sky;

class VisualObjects final {
  public:
    VisualObjects(const SceneGlobals& globals, const std::pair<Tempest::Vec3, Tempest::Vec3>& bbox);
    ~VisualObjects();

    ObjectsBucket::Item get(const StaticMesh& mesh, const Material& mat, size_t iboOffset, size_t iboLength,
                            const ProtoMesh* anim, bool staticDraw);
    ObjectsBucket::Item get(const AnimMesh&   mesh, const Material& mat,
                            const MatrixStorage::Id& anim,
                            size_t ibo, size_t iboLen);
    ObjectsBucket::Item get(const Tempest::VertexBuffer<Resources::Vertex>& vbo,
                            const Tempest::IndexBuffer<uint32_t>& ibo,
                            size_t iboOff, size_t iboLen,
                            const Tempest::AccelerationStructure* blas,
                            const Tempest::StorageBuffer* desc,
                            const Material& mat, const Bounds& bbox, ObjectsBucket::Type bucket);
    ObjectsBucket::Item get(const Tempest::VertexBuffer<Resources::Vertex>* vbo[],
                            const Material& mat, const Bounds& bbox);

    MatrixStorage::Id   getMatrixes(Tempest::BufferHeap heap, size_t boneCnt);
    auto                matrixSsbo (Tempest::BufferHeap heap, uint8_t fId) const -> const Tempest::StorageBuffer&;

    void setupUbo();
    void preFrameUpdate(uint8_t fId);
    void visibilityPass(const Frustrum fr[]);
    void draw          (Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t fId);
    void drawGBuffer   (Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t fId);
    void drawShadow    (Tempest::Encoder<Tempest::CommandBuffer>& enc, uint8_t fId, int layer=0);

    void resetIndex();
    void resetTlas();
    void recycle(Tempest::DescriptorSet&& del);

    void setLandscapeBlas(const Tempest::AccelerationStructure* blas);
    Tempest::Signal<void(const Tempest::AccelerationStructure* tlas)> onTlasChanged;

  private:
    ObjectsBucket&                  getBucket(const Material& mat, ObjectsBucket::Type type,
                                              const ProtoMesh* anim, const Tempest::StorageBuffer* desc, const StaticMesh* hint);
    void                            mkIndex();
    void                            commitUbo(uint8_t fId);
    void                            mkTlas(uint8_t fId);

    const SceneGlobals&             globals;
    VisibilityGroup                 visGroup;
    MatrixStorage                   matrix;

    std::vector<std::unique_ptr<ObjectsBucket>> buckets;
    std::vector<ObjectsBucket*>                 index;
    size_t                                      lastSolidBucket = 0;

    std::vector<Tempest::DescriptorSet>         recycled[Resources::MaxFramesInFlight];
    uint8_t                                     recycledId = 0;

    bool                                        needtoInvalidateTlas = false;
    Tempest::AccelerationStructure              tlas;
    const Tempest::AccelerationStructure*       landBlas = nullptr;

  friend class ObjectsBucket;
  friend class ObjectsBucket::Item;
  };

