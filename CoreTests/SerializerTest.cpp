#include <Serialization.h>
#include <Hasher.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMap.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAString.h>
#include <unordered_map>
#include <CASTL/CASharedPtr.h>


struct TestStruct1
{
	castl::shared_ptr<float> testV;
	float aa;
	float bb;
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
	float cc[5]{ 1, 2, 3, 5 };
	float bb;

	CA_PRIVATE_REFLECTION(TestStruct2);
};

//CA_REFLECTION(TestStruct1, aa, bb);
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

int main(int argc, char* argv[])
{
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

	castl::unordered_map<TestStruct2, int, cacore::hash<TestStruct2>> tstMap2;
	cacore::deserializer<decltype(byteBuffer)> deserializer(byteBuffer);
	deserializer.deserialize(tstMap2);

	TestStruct1 testStruct2{ testFloat, 1.0f, 2.0f };
	std::cout << careflection::aggregate_member_count<TestStruct1>() << std::endl;
	//static_assert(careflection::aggregate_member_count<TestStruct1>() == 2, "Why");

	byteBuffer.clear();
	cacore::serialize(byteBuffer, testStruct2);
	TestStruct1 testStruct3;
	cacore::deserializer<decltype(byteBuffer)> deserializer1(byteBuffer);
	deserializer1.deserialize(testStruct3);
	return 0;
}