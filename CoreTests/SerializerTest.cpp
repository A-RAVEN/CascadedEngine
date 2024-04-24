#include <Serialization.h>
#include <Hasher.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAMap.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAString.h>
#include <unordered_map>

int main(int argc, char* argv[])
{
	struct TestStruct
	{
		castl::map<castl::string, int> map;
		castl::unordered_map<castl::string, int> map1;
		int a;
	};

	struct TestStruct1
	{
		float aa;
		float bb;
	};

	std::unordered_map<TestStruct1, int> tstMap1 = { {{1.0f, 2.0f}, 3} };

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