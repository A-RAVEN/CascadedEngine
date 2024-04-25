#include <Serialization.h>
#include <Hasher.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMap.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAString.h>
#include <unordered_map>



struct TestStruct1
{
	float aa;
	float bb;
	float cc[5];
	auto operator <=>(const TestStruct1&) const = default;
};

template<typename T>
struct TypeDescriptor
{
};

template<typename T, size_t Offset>
struct TypeMemberDesc
{
	using type = T;
	static constexpr size_t offset = Offset;
};

template<>
struct TypeDescriptor<TestStruct1>
{
	using type = TestStruct1;
	using member_tuple_type = std::tuple <
		TypeMemberDesc<decltype(TestStruct1::aa), offsetof(TestStruct1, aa) >
		, TypeMemberDesc<decltype(TestStruct1::bb), offsetof(TestStruct1, bb) >
		, TypeMemberDesc<decltype(TestStruct1::cc), offsetof(TestStruct1, cc) >
	>;
	constexpr static size_t member_count = std::tuple_size_v<member_tuple_type>;
};

template<typename T>
concept has_type_desc = requires { TypeDescriptor<T>::member_count; };

template<typename T, size_t index>
constexpr void evaluate_type() requires has_type_desc<T>
{
	constexpr auto structSize = TypeDescriptor<TestStruct1>::member_count;
	using member_tuple_type = TypeDescriptor<TestStruct1>::member_tuple_type;
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
	int structSize = TypeDescriptor<TestStruct1>::member_count;

	evaluate_type<TestStruct1, 0>();

	TypeDescriptor<TestStruct1>::member_tuple_type memberTuple;
	struct TestStruct
	{
		castl::map<castl::string, int> map;
		castl::unordered_map<castl::string, int> map1;
		int a;
	};

	std::unordered_map<TestStruct1, int, cahasher::hash<TestStruct1>> tstMap1 = { {{1.0f, 2.0f}, 3} };

	TestStruct testStruct = {};
	testStruct.map = {
		{"a", 1},
		{"b", 2},
		{"c", 3}
	};
	testStruct.map1 = {
		{"d", 4},
		{"e", 5},
		{"f", 6}
	};
	testStruct.a = 3;

	castl::map<castl::string, int> tstMap;

	std::unordered_map<castl::string, int, cahasher::hash<castl::string>> tstMap2 = { {"a", 1}, {"b", 2}, {"c", 3} };

	castl::vector<uint8_t> result;
	cacore::serializer<castl::vector<uint8_t>> writer{result};
	writer.serialize(testStruct);

	castl::string testStr = "aaaa";
	testStr[0] = 'a';

	castl::pair<castl::string, int> pr = {"a", 1};
	auto&& [key, value] = pr;

	TestStruct testStruct1{};
	cacore::deserializer<castl::vector<uint8_t>> loader{ result };
	loader.deserialize(testStruct1);

	return 0;
}