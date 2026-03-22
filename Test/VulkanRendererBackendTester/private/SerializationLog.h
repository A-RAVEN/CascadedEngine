#pragma once
namespace resource_management
{
	static void LogZPPError(const std::string& details, std::errc error_code)
	{
		std::string value_name;
		switch (error_code)
		{
		case std::errc::result_out_of_range:
			value_name = "attempting to write or read from a too short buffer";
			break;
		case std::errc::no_buffer_space:
			value_name = "growing buffer would grow beyond the allocation limits or overflow";
			break;
		case std::errc::value_too_large:
			value_name = "varint (variable length integer) encoding is beyond the representation limits";
			break;
		case std::errc::message_size:
			value_name = "message size is beyond the user defined allocation limits";
			break;
		case std::errc::not_supported:
			value_name = "attempt to call an RPC that is not listed as supported";
			break;
		case std::errc::bad_message:
			value_name = "attempt to read a variant of unrecognized type";
			break;
		case std::errc::invalid_argument:
			value_name = "attempting to serialize null pointer or a value-less variant";
			break;
		case std::errc::protocol_error:
			value_name = "attempt to deserialize an invalid protocol message";
			break;
		}
		std::cout << details << ":\n  "
			<< " (" << value_name << ")\n\n";
	}
}