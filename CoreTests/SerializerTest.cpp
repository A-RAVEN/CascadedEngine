#include <Serialization.h>
#include <Hasher.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMap.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAString.h>
#include <unordered_map>
#include <CASTL/CASharedPtr.h>
#include <glm/glm.hpp>

template<glm::length_t L, typename T, glm::qualifier Q>
struct careflection::containerInfo<glm::vec<L, T, Q>>
{
	using elementType = glm::vec<L, T, Q>::value_type;
	constexpr static auto container_size(const glm::vec<L, T, Q>& container)
	{
		return L;
	}
};

template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
struct careflection::containerInfo<glm::mat<C, R, T, Q>>
{
	using elementType = glm::mat<C, R, T, Q>::col_type;
	constexpr static auto container_size(const glm::mat<C, R, T, Q>& container)
	{
		return C;
	}
};

struct TestTruct0
{
	glm::vec4 a;
	glm::vec3 b;
	glm::vec2 c;
	glm::mat4 testMat;
	auto operator <=>(const TestTruct0&) const = default;
};

struct TestStruct1
{
	castl::shared_ptr<float> testV;
	float aa;
	float bb;
	float* pcc;
	auto operator <=>(const TestStruct1&) const = default;
};

class TestStruct2
{
public:
	auto operator <=>(const TestStruct2&) const = default;
	TestStruct2() = default;
	TestStruct2(float a, float b) : aa(a), bb(b) { }
	castl::shared_ptr<float> testV;
private:
	float aa;
	float cc[5]{ 1, 2, 3, 5, 6 };
	float bb;

	CA_PRIVATE_REFLECTION(TestStruct2);
};

CA_REFLECTION(TestStruct2, testV, aa, bb, cc);

template<typename T, size_t index>
constexpr void evaluate_type() requires careflection::has_type_desc<T>
{
	constexpr auto structSize = CATypeDescriptor<T>::member_count;
	using member_tuple_type = CATypeDescriptor<T>::member_tuple_type;
	if constexpr (index < structSize)
	{
		using visitingElementType = std::tuple_element_t<index, member_tuple_type>;
		std::cout << typeid(typename visitingElementType::type).name() << std::endl;
		std::cout << visitingElementType::offset << std::endl;
		evaluate_type<T, index + 1>();
	}
}

struct TestStruct3
{
	int a;
	castl::string test2;
	auto operator==(const TestStruct3& other) const
	{
		return test2 == other.test2;
	}

	friend constexpr void ca_hash(TestStruct3 const& obj, auto& hasher)
	{
		hasher.hash(obj.test2);
	}
};

void TestHash()
{
	static_assert(careflection::containerInfo<glm::vec3>::container_size(glm::vec3{ 1, 1, 1 }) == 3);
	TestTruct0 testStructIn = TestTruct0{ {0, 0, 0, 0}, {1, 1, 1}, {2, 2}, glm::mat4(1.0f)};
	castl::unordered_map<TestTruct0, int> tstMap3;
	tstMap3.insert({ testStructIn, 3 });

	castl::vector<uint8_t> byteBuffer;
	cacore::serialize(byteBuffer, testStructIn);
	TestTruct0 testStructOut;
	cacore::HashObj<TestTruct0> testStructOut1;
	cacore::deserialize(byteBuffer, testStructOut);
	cacore::deserialize(byteBuffer, testStructOut1);
}

void TestHash1()
{
	cacore::HashObj<TestStruct2> testStructIn = TestStruct2{ 1.0f, 2.0f };
	constexpr bool has_hash = cacore::has_custom_hash_func<cacore::HashObj<TestStruct2>, cacore::defaultHasher<>>;
	constexpr bool has_hash1 = cacore::has_custom_hash_func<TestStruct2, cacore::defaultHasher<>>;
	castl::unordered_map<cacore::HashObj<TestStruct2>, int> tstMap3;
	tstMap3.insert({ testStructIn, 3 });

	castl::vector<uint8_t> byteBuffer;
	cacore::serialize(byteBuffer, testStructIn);
	TestStruct2 testStructOut;
	cacore::HashObj<TestStruct2> testStructOut1;
	cacore::deserialize(byteBuffer, testStructOut);
	cacore::deserialize(byteBuffer, testStructOut1);
}

void TestHash2()
{
	TestStruct3 testStruct3{ 1, "test" };
	castl::unordered_map<TestStruct3, int> testMap;
	testMap.insert({ testStruct3, 3 });

	castl::vector<uint8_t> byteBuffer;
	cacore::serialize(byteBuffer, testStruct3);
	TestStruct3 testStruct4;
	cacore::deserializer<decltype(byteBuffer)> deserializer(byteBuffer);
	deserializer.deserialize(testStruct4);
}

int main(int argc, char* argv[])
{
	TestHash();
	TestHash1();
	TestHash2();

	//evaluate_type<TestStruct1, 0>();
	evaluate_type<TestStruct2, 0>();

	struct TestStruct
	{
		castl::map<castl::string, int> map;
		castl::unordered_map<castl::string, int> map1;
		int a;
	};


	std::unordered_map<TestStruct2, int, cacore::hash<TestStruct2>> tstMap1;
	castl::shared_ptr<float> testFloat = castl::make_shared<float>(3.0f);

	TestStruct2 testStruct1 = { 1, 2 };
	testStruct1.testV = testFloat;

	tstMap1.insert({ testStruct1, 3 });

	castl::vector<uint8_t> byteBuffer;

	cacore::serialize(byteBuffer, tstMap1);

	castl::unordered_map<TestStruct2, int> tstMap2;
	cacore::deserializer<decltype(byteBuffer)> deserializer(byteBuffer);
	deserializer.deserialize(tstMap2);

	TestStruct1 testStruct2{ testFloat, 1.0f, 2.0f, nullptr };
	castl::unordered_map<TestStruct1, int> tstMap3;
	tstMap3.insert({ testStruct2, 3 });
	std::cout << careflection::aggregate_member_count<TestStruct1>() << std::endl;
	//static_assert(careflection::aggregate_member_count<TestStruct1>() == 2, "Why");

	static_assert(std::is_trivially_copyable_v<glm::vec4>, "vec4 not trivially copyable");

	byteBuffer.clear();
	cacore::serialize(byteBuffer, testStruct2);
	TestStruct1 testStruct3;
	cacore::deserializer<decltype(byteBuffer)> deserializer1(byteBuffer);
	deserializer1.deserialize(testStruct3);
	return 0;
}