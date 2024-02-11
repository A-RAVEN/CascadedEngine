#pragma once
#include <mutex>
namespace castl
{
	using std::mutex;
	using std::recursive_mutex;
	using std::lock_guard;
	using std::unique_lock;
	using std::condition_variable;
}