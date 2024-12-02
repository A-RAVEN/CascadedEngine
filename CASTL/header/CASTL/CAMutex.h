#pragma once
#include <mutex>
#include <shared_mutex>
namespace castl
{
	using std::mutex;
	using std::shared_mutex;
	using std::recursive_mutex;
	using std::lock_guard;
	using std::unique_lock;
	using std::shared_lock;
	using std::condition_variable;
}