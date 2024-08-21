#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
using namespace JPH;
#include <CASTL/CALockedDeque.h>
#include <CASTL/CAVector.h>
#include <ThreadManager.h>


/// Layer that objects can be in, determines which other objects it can collide with
namespace Layers
{
	static constexpr ObjectLayer UNUSED1 = 0; // 4 unused values so that broadphase layers values don't match with object layer values (for testing purposes)
	static constexpr ObjectLayer UNUSED2 = 1;
	static constexpr ObjectLayer UNUSED3 = 2;
	static constexpr ObjectLayer UNUSED4 = 3;
	static constexpr ObjectLayer NON_MOVING = 4;
	static constexpr ObjectLayer MOVING = 5;
	static constexpr ObjectLayer DEBRIS = 6; // Example: Debris collides only with NON_MOVING
	static constexpr ObjectLayer SENSOR = 7; // Sensors only collide with MOVING objects
	static constexpr ObjectLayer NUM_LAYERS = 8;
};

/// Broadphase layers
namespace BroadPhaseLayers
{
	static constexpr BroadPhaseLayer NON_MOVING(0);
	static constexpr BroadPhaseLayer MOVING(1);
	static constexpr BroadPhaseLayer DEBRIS(2);
	static constexpr BroadPhaseLayer SENSOR(3);
	static constexpr BroadPhaseLayer UNUSED(4);
	static constexpr uint NUM_LAYERS(5);
};


/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
{
public:
	virtual bool					ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
	{
		switch (inObject1)
		{
		case Layers::UNUSED1:
		case Layers::UNUSED2:
		case Layers::UNUSED3:
		case Layers::UNUSED4:
			return false;
		case Layers::NON_MOVING:
			return inObject2 == Layers::MOVING || inObject2 == Layers::DEBRIS;
		case Layers::MOVING:
			return inObject2 == Layers::NON_MOVING || inObject2 == Layers::MOVING || inObject2 == Layers::SENSOR;
		case Layers::DEBRIS:
			return inObject2 == Layers::NON_MOVING;
		case Layers::SENSOR:
			return inObject2 == Layers::MOVING;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};


/// BroadPhaseLayerInterface implementation
class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
{
public:
	BPLayerInterfaceImpl()
	{
		// Create a mapping table from object to broad phase layer
		mObjectToBroadPhase[Layers::UNUSED1] = BroadPhaseLayers::UNUSED;
		mObjectToBroadPhase[Layers::UNUSED2] = BroadPhaseLayers::UNUSED;
		mObjectToBroadPhase[Layers::UNUSED3] = BroadPhaseLayers::UNUSED;
		mObjectToBroadPhase[Layers::UNUSED4] = BroadPhaseLayers::UNUSED;
		mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
		mObjectToBroadPhase[Layers::DEBRIS] = BroadPhaseLayers::DEBRIS;
		mObjectToBroadPhase[Layers::SENSOR] = BroadPhaseLayers::SENSOR;
	}

	virtual uint					GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::NUM_LAYERS;
	}

	virtual BroadPhaseLayer			GetBroadPhaseLayer(ObjectLayer inLayer) const override
	{
		JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
		return mObjectToBroadPhase[inLayer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
	{
		switch ((BroadPhaseLayer::Type)inLayer)
		{
		case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
		case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
		case (BroadPhaseLayer::Type)BroadPhaseLayers::DEBRIS:		return "DEBRIS";
		case (BroadPhaseLayer::Type)BroadPhaseLayers::SENSOR:		return "SENSOR";
		case (BroadPhaseLayer::Type)BroadPhaseLayers::UNUSED:		return "UNUSED";
		default:													JPH_ASSERT(false); return "INVALID";
		}
	}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
	BroadPhaseLayer					mObjectToBroadPhase[Layers::NUM_LAYERS];
};


/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
{
public:
	virtual bool					ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1)
		{
		case Layers::NON_MOVING:
			return inLayer2 == BroadPhaseLayers::MOVING || inLayer2 == BroadPhaseLayers::DEBRIS;
		case Layers::MOVING:
			return inLayer2 == BroadPhaseLayers::NON_MOVING || inLayer2 == BroadPhaseLayers::MOVING || inLayer2 == BroadPhaseLayers::SENSOR;
		case Layers::DEBRIS:
			return inLayer2 == BroadPhaseLayers::NON_MOVING;
		case Layers::SENSOR:
			return inLayer2 == BroadPhaseLayers::MOVING;
		case Layers::UNUSED1:
		case Layers::UNUSED2:
		case Layers::UNUSED3:
			return false;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};



class JoltJobWrapper : public JobSystem
{
	
	class JoltBarrierWrapper : public Barrier
	{
		// 通过 Barrier 继承
		void AddJob(const JobHandle& inJob) override
		{
			inJob.GetPtr();
		}
		void AddJobs(const JobHandle* inHandles, uint inNumHandles) override
		{
		}
		void OnJobFinished(Job* inJob) override
		{
		}
		castl::vector<thread_management::TaskBase*> m_Tasks;
	};

	// 通过 JobSystem 继承
	int GetMaxConcurrency() const override
	{
		return 0;
	}
	JobHandle CreateJob(const char* inName, ColorArg inColor, const JobFunction& inJobFunction, uint32 inNumDependencies) override
	{
		auto newJob = new Job(inName, inColor, this, inJobFunction, inNumDependencies);
		return JobHandle(newJob);
	}
	Barrier* CreateBarrier() override
	{
		return new JoltBarrierWrapper();
	}
	void DestroyBarrier(Barrier* inBarrier) override
	{
		delete static_cast<JoltBarrierWrapper*>(inBarrier);
	}
	void WaitForJobs(Barrier* inBarrier) override
	{
	}
	void QueueJob(Job* inJob) override
	{
		auto task = pTaskScheduler->NewTask()
			->Name("Jolt SubTask")
			->Functor([inJob]()
				{
					inJob->Execute();
					inJob->Release();
				});
		pTaskScheduler->Execute(task);
	}
	void QueueJobs(Job** inJobs, uint inNumJobs) override
	{
		castl::vector<thread_management::TaskBase*> tasks;
		tasks.reserve(inNumJobs);
		for (uint i = 0; i < inNumJobs; ++i)
		{
			auto task = pTaskScheduler->NewTask()
				->Name("Jolt SubTask")
				->Functor([pJob = inJobs[i]]()
					{
						pJob->Execute();
						pJob->Release();
					});
			tasks.push_back(task);
		}
		pTaskScheduler->Execute(tasks);
	}
	void FreeJob(Job* inJob) override
	{
		delete inJob;
	}
private:
	thread_management::TaskScheduler* pTaskScheduler;
};


class JoltTest
{
public:
	PhysicsSettings mPhysicsSettings;

	BPLayerInterfaceImpl mBroadPhaseLayerInterface;
	ObjectVsBroadPhaseLayerFilterImpl mObjectVsBroadPhaseLayerFilter;
	ObjectLayerPairFilterImpl mObjectVsObjectLayerFilter;

	void Run()
	{
		PhysicsSystem* m_PhysicsSystem = new PhysicsSystem();

		static constexpr uint cNumBodies = 10240;
		static constexpr uint cNumBodyMutexes = 0; // Autodetect
		static constexpr uint cMaxBodyPairs = 65536;
		static constexpr uint cMaxContactConstraints = 20480;

		m_PhysicsSystem->Init(cNumBodies
			, cNumBodyMutexes
			, cMaxBodyPairs
			, cMaxContactConstraints
			, mBroadPhaseLayerInterface
			, mObjectVsBroadPhaseLayerFilter
			, mObjectVsObjectLayerFilter);
		m_PhysicsSystem->SetPhysicsSettings(mPhysicsSettings);

		// Restore gravity
		m_PhysicsSystem->SetGravity(Vec3(0.0f, -9.8f, 0.0f));
	}
};