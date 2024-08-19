//#pragma once
//#include <CASTL/CAString.h>
//#include <CASTL/CASharedPtr.h>
//#include "Common.h"
//#include "TextureSampler.h"
//#include "GPUTexture.h"
//#include "GPUBuffer.h"
//#include "ShaderBindingBuilder.h"
//
//namespace graphics_backend
//{
//	class ShaderConstantSet
//	{
//	public:
//		template<typename T>
//		void SetValue(castl::string const& name, T const& value)
//		{
//			SetValue(name, (void*)&value);
//		}
//		virtual void SetValue(castl::string const& name, void* pValue) = 0;
//		virtual castl::string const& GetName() const = 0;
//	};
//
//	class ShaderBindingSet
//	{
//	public:
//		virtual void SetName(castl::string const& name) = 0;
//		virtual void SetConstantSet(castl::string const& name, castl::shared_ptr<ShaderConstantSet> const& pConstantSet) = 0;
//		virtual void SetStructBuffer(castl::string const& name, castl::shared_ptr<GPUBuffer> const& pBuffer) = 0;
//		virtual void SetTexture(castl::string const& name
//			, castl::shared_ptr<GPUTexture> const& pTexture) = 0;
//		//virtual void SetSampler(castl::string const& name
//		//	, castl::shared_ptr<TextureSampler> const& pSampler) = 0;
//		virtual bool UploadingDone() const = 0;
//		virtual ShaderBindingBuilder const& GetBindingSetDesc() const = 0;
//	};
//}