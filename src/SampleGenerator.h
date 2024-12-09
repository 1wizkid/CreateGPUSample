#pragma once
//#include <mfapi.h>

using namespace Microsoft::WRL; //this is needed to use ComPtr<>

class VideoProcessorClass
{
public:
	/// <summary>
	/// Must call Initialize() before using this object
	/// </summary>
	/// <param name="p_pIMFDXGIDeviceManager"></param>
	VideoProcessorClass()
	{
		return;
	}
	/// <summary>
	/// Call this function before using this class.
	/// </summary>
	/// <returns></returns>
	HRESULT Initialize(IMFDXGIDeviceManager* p_pIMFDXGIDeviceManager)
	{
		HRESULT r_hrReturn = S_FALSE;
		ComPtr<IMFAttributes> l_Attributes;

		do
		{
			r_hrReturn = CoCreateInstance(CLSID_VideoProcessorMFT,
									nullptr,
									CLSCTX_INPROC_SERVER,
									IID_IMFTransform,
									(void**)&m_IMFTransform);
			if (FAILED(r_hrReturn))
			{
				break;
			}

			r_hrReturn = m_IMFTransform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
												ULONG_PTR(p_pIMFDXGIDeviceManager));
			if (FAILED(r_hrReturn))
			{
				break;
			}

			// Tell the XVP that we are the swapchain allocator
			r_hrReturn = m_IMFTransform->GetAttributes(&l_Attributes);
			if (FAILED(r_hrReturn))
			{
				break;
			}

			r_hrReturn = l_Attributes->SetUINT32(MF_TOPOLOGY_ENABLE_XVP_FOR_PLAYBACK, //was MF_XVP_PLAYBACK_MODE but that is undefined. I beleive this is the replacement. richfr todo confirm that
													TRUE);
			if (FAILED(r_hrReturn))
			{
				break;
			}

			r_hrReturn = m_IMFTransform->QueryInterface(IID_PPV_ARGS(&m_IMFVideoProcessorControl));
			if (FAILED(r_hrReturn))
			{
				break;
			}
		} while (FALSE);
		return r_hrReturn;
	}
	/// <summary>
	/// call this just prior to starting to stream.
	/// </summary>
	/// <returns></returns>
	HRESULT Start()
	{
		HRESULT r_hrReturn = S_FALSE;
		//dont really care about this return value, it is the begin that is important
		//r_hrReturn = m_IMFTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);//make sure we are not already streaming. richfr todo: confirm this is ok to do
		//Set the rectangular area on the destination buffer we want the source content blitted to.
		RECT l_DestRect;

		CopyRect(&l_DestRect, &m_InputdisplayRect);
		//l_DestRect.right /= 2;//half it
		//l_DestRect.bottom /= 2;//half it
		r_hrReturn = m_IMFVideoProcessorControl->SetDestinationRectangle(&l_DestRect);//for now, this is set to the actual video frame size from the stream itself
		if (SUCCEEDED(r_hrReturn))
		{
			//Now select the portion of the input frame you want blitted to the Destination rectangle size set on the destination frame
			RECT l_SourceRect;

			CopyRect(&l_SourceRect, &m_InputdisplayRect);
			//l_SourceRect.right /= 2;//half it
			//l_SourceRect.bottom /= 2;//half it

			r_hrReturn = m_IMFVideoProcessorControl->SetSourceRectangle(&l_SourceRect);//for now, the source and destination will be the same size
			if (SUCCEEDED(r_hrReturn))
			{ 
				r_hrReturn = m_IMFTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
			}
		}
		return r_hrReturn;
	}
	
	
	/// <summary>
	/// Pass in a sample and this will feed it through the VideoProcessor and return a new sample with the processed image
	/// </summary>
	/// <param name="p_pIMFSampleInput"></param>
	/// <param name="p_IMFSampleOutput"></param>
	/// <returns></returns>
	HRESULT GetSample(IMFSample* p_pIMFSampleInput,
						IMFSample** p_IMFSampleOutput)
	{
		HRESULT r_hrReturn = S_FALSE;
		DWORD dwStatus = 0;
		MFT_OUTPUT_DATA_BUFFER outputDataBuffer = {};
		outputDataBuffer.dwStreamID = 0;
		outputDataBuffer.dwStatus = 0;
		outputDataBuffer.pEvents = nullptr;
		outputDataBuffer.pSample = nullptr;	//we want the caller to allocate the output media sample. richfr to do there is a flag to confirm videoproc can do this (MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES )
		
		r_hrReturn = m_IMFTransform->ProcessOutput(0,//flags. We don't need to set any
													1,//Buffer count we are passing in which is just one
													&outputDataBuffer,//the single output buffer we are passing in
													&dwStatus);//note: we expect dwStatus to return 0 as we don't currently handle the new stream return value
		if (r_hrReturn == MF_E_TRANSFORM_NEED_MORE_INPUT)
		{
				//Provide the sample passed in to this function to the videoproc to process
				r_hrReturn = m_IMFTransform->ProcessInput(0,//stream id
															p_pIMFSampleInput,//the sample to process
															0);//flags. Must be 0
		}
		//if the call above to process output worked, then r_hrReturn will be S_OK
		//if it was need more input and we passed it a buffer to process then r_hrReturn will be S_OK
		//if any other value at this point then we fail
		if (SUCCEEDED(r_hrReturn))
		{
			//We should have a buffer to read now.
			//We expect 1 in 1 out so if that doesn't happen this algorithm won't work as
			//it is specifically designed for uncompressed video frames that have that behaviour with the decoder
			r_hrReturn = m_IMFTransform->ProcessOutput(0,//MFT_PROCESS_OUTPUT_REGENERATE_LAST_OUTPUT,//flags. We don't need to set any
														1,//Buffer count we are passing in which is just one
														&outputDataBuffer,//the single output buffer we are passing in
														&dwStatus);//note: we expect dwStatus to return 0 as we don't currently handle the new stream return value

			if (SUCCEEDED(r_hrReturn))
			{
				//Success. Return the output sample
				*p_IMFSampleOutput = outputDataBuffer.pSample;//richfr todo: I don't put a addref on this. I assume it already has one.
				//If any events returned, release them. We currently do not process them
				if (outputDataBuffer.pEvents != nullptr)
				{
					outputDataBuffer.pEvents->Release();
					outputDataBuffer.pEvents = nullptr;
				}
			}
		}//processinput returned a buffer
	return r_hrReturn;
	}
	/// <summary>
	/// Tell the XVP about the size of the destination surface
	/// and where within the surface we should be writing.
	/// </summary>
	/// <param name="pType"></param>
	/// <param name="vpOutputFormat"></param>
	/// <returns></returns>
	HRESULT SetOutputMediaType(IMFMediaType* p_pOutMediaType)
	{
		HRESULT hr = S_OK;
		
		hr = m_IMFTransform->SetOutputType(0,	//streamID
											p_pOutMediaType, //MediaType to output
												0); //Flags: 0 = set this value
		return hr;
	}
	HRESULT SetInputMediaType(IMFMediaType* p_pInMediaType)
	{
		HRESULT hr = S_OK;
		ComPtr<IMFAttributes> l_IMFAttributes;


		//richfr removed. todo confirm when this is needed CAutoLock lock(&m_critSec);

		do
		{
			/* todo when is this needed
			hr = CheckShutdown();
			if (FAILED(hr))
			{
				break;
			}
			*/
			hr = p_pInMediaType->QueryInterface(IID_IMFAttributes,
												 &l_IMFAttributes);
			if (FAILED(hr))
			{
				break;
			}
			/* richfr we don't care about 3d
			HRESULT hr1 = pAttributes->GetUINT32(MF_MT_VIDEO_3D, (UINT32*)&m_b3DVideo);
			if (SUCCEEDED(hr1))
			{
				hr = pAttributes->GetUINT32(MF_MT_VIDEO_3D_FORMAT, (UINT32*)&m_vp3DOutput);
				if (FAILED(hr))
				{
					break;
				}
			}
			*/
			//Now Determine Correct Display Resolution
			UINT32 parX = 0, parY = 0;
			int PARWidth = 0, PARHeight = 0;
			MFVideoArea videoArea = { 0 };
			ZeroMemory(&m_InputdisplayRect, sizeof(RECT));

			if (FAILED(MFGetAttributeSize(p_pInMediaType, MF_MT_PIXEL_ASPECT_RATIO, &parX, &parY)))
			{
				parX = 1;
				parY = 1;
			}

			hr = GetVideoDisplayArea(p_pInMediaType, &videoArea);//this sets the class variables for width and heighth
			if (FAILED(hr))
			{
				break;
			}

			m_InputdisplayRect = MFVideoAreaToRect(videoArea);

			PixelAspectToPictureAspect(
				videoArea.Area.cx,
				videoArea.Area.cy,
				parX,
				parY,
				&PARWidth,
				&PARHeight);

			SIZE szVideo = videoArea.Area;
			SIZE szPARVideo = { PARWidth, PARHeight };
			AspectRatioCorrectSize(&szVideo, szPARVideo, videoArea.Area, FALSE);
			m_uiRealDisplayWidth = szVideo.cx;
			m_uiRealDisplayHeight = szVideo.cy;

			if (SUCCEEDED(hr))
			{
				// set the input type on the XVP
				hr = m_IMFTransform->SetInputType(0,
													p_pInMediaType,
												  0);
				if (FAILED(hr))
				{
					break;
				}
			}
		} while (FALSE);

		return hr;
	}

private:
	ComPtr<IMFVideoProcessorControl> m_IMFVideoProcessorControl;
	ComPtr<IMFTransform> m_IMFTransform;
	RECT m_InputdisplayRect = { 0,0,0,0 };
	UINT32 m_imageWidthInPixels = 0;
	UINT32 m_imageHeightInPixels = 0;
	UINT32 m_uiRealDisplayWidth = 0;
	UINT32 m_uiRealDisplayHeight = 0;
	
	HRESULT GetVideoDisplayArea(IMFMediaType* pType,
		MFVideoArea* pArea)
	{
		HRESULT hr = S_OK;
		BOOL bPanScan = FALSE;
		UINT32 uimageWidthInPixels = 0;
		UINT32 uimageHeightInPixels = 0;

		hr = MFGetAttributeSize(pType,
			MF_MT_FRAME_SIZE,
			&uimageWidthInPixels,
			&uimageHeightInPixels);
		if (FAILED(hr))
		{
			return hr;
		}

		if (uimageWidthInPixels != m_imageWidthInPixels || uimageHeightInPixels != m_imageHeightInPixels)
		{
			//SafeRelease(m_pVideoProcessorEnum);
			//SafeRelease(m_pVideoProcessor);
			//SafeRelease(m_pSwapChain1);
		}

		m_imageWidthInPixels = uimageWidthInPixels;
		m_imageHeightInPixels = uimageHeightInPixels;

		bPanScan = MFGetAttributeUINT32(pType,
			MF_MT_PAN_SCAN_ENABLED,
			FALSE);

		// In pan/scan mode, try to get the pan/scan region.
		if (bPanScan)
		{
			hr = pType->GetBlob(
				MF_MT_PAN_SCAN_APERTURE,
				(UINT8*)pArea,
				sizeof(MFVideoArea),
				NULL
			);
		}

		// If not in pan/scan mode, or the pan/scan region is not set,
		// get the minimimum display aperture.

		if (!bPanScan || hr == MF_E_ATTRIBUTENOTFOUND)
		{
			hr = pType->GetBlob(
				MF_MT_MINIMUM_DISPLAY_APERTURE,
				(UINT8*)pArea,
				sizeof(MFVideoArea),
				NULL
			);

			if (hr == MF_E_ATTRIBUTENOTFOUND)
			{
				// Minimum display aperture is not set.

				// For backward compatibility with some components,
				// check for a geometric aperture.

				hr = pType->GetBlob(
					MF_MT_GEOMETRIC_APERTURE,
					(UINT8*)pArea,
					sizeof(MFVideoArea),
					NULL
				);
			}

			// Default: Use the entire video area.

			if (hr == MF_E_ATTRIBUTENOTFOUND)
			{
				*pArea = MakeArea(0.0, 0.0, m_imageWidthInPixels, m_imageHeightInPixels);
				hr = S_OK;
			}
		}

		return hr;
	}
	/// <summary>
	/// Corrects the supplied size structure so that it becomes the same shape
	/// as the specified aspect ratio, the correction is always applied in the
	/// horizontal axis
	///
	/// </summary>
	/// <param name="lpSizeImage"></param>
	/// <param name="sizeAr"></param>
	/// <param name="sizeOrig"></param>
	/// <param name="ScaleXorY"></param>
	void AspectRatioCorrectSize(
		LPSIZE lpSizeImage,     // size to be aspect ratio corrected
		const SIZE& sizeAr,     // aspect ratio of image
		const SIZE& sizeOrig,   // original image size
		BOOL ScaleXorY          // axis to correct in
	)
	{
		int cxAR = sizeAr.cx;
		int cyAR = sizeAr.cy;
		int cxOr = sizeOrig.cx;
		int cyOr = sizeOrig.cy;
		int sx = lpSizeImage->cx;
		int sy = lpSizeImage->cy;

		// MulDiv rounds correctly.
		lpSizeImage->cx = MulDiv((sx * cyOr), cxAR, (cyAR * cxOr));

		if (ScaleXorY && lpSizeImage->cx < cxOr)
		{
			lpSizeImage->cx = cxOr;
			lpSizeImage->cy = MulDiv((sy * cxOr), cyAR, (cxAR * cyOr));
		}
		return;
	}

	/// <summary>
	///  Converts a pixel aspect ratio to a picture aspect ratio
	/// </summary>
	void PixelAspectToPictureAspect(
		int Width,
		int Height,
		int PixelAspectX,
		int PixelAspectY,
		int* pPictureAspectX,
		int* pPictureAspectY)
	{
		//
		// sanity check - if any inputs are 0, return 0
		//
		if (PixelAspectX == 0 || PixelAspectY == 0 || Width == 0 || Height == 0)
		{
			*pPictureAspectX = 0;
			*pPictureAspectY = 0;
			return;
		}
		//
   // start by reducing both ratios to lowest terms
   //
		ReduceToLowestTerms(Width, Height, &Width, &Height);
		ReduceToLowestTerms(PixelAspectX, PixelAspectY, &PixelAspectX, &PixelAspectY);

		//
		// Make sure that none of the values are larger than 2^16, so we don't
		// overflow on the last operation.   This reduces the accuracy somewhat,
		// but it's a "hail mary" for incredibly strange aspect ratios that don't
		// exist in practical usage.
		//
		while (Width > 0xFFFF || Height > 0xFFFF)
		{
			Width >>= 1;
			Height >>= 1;
		}

		while (PixelAspectX > 0xFFFF || PixelAspectY > 0xFFFF)
		{
			PixelAspectX >>= 1;
			PixelAspectY >>= 1;
		}

		ReduceToLowestTerms(
			PixelAspectX * Width,
			PixelAspectY * Height,
			pPictureAspectX,
			pPictureAspectY
		);
		return;
	}
	void ReduceToLowestTerms(int NumeratorIn,
							int DenominatorIn,
							int* pNumeratorOut,
							int* pDenominatorOut)
	{
		int GCD = gcd(NumeratorIn, DenominatorIn);

		*pNumeratorOut = NumeratorIn / GCD;
		*pDenominatorOut = DenominatorIn / GCD;
		return;
	}

	// returns the greatest common divisor of A and B
	inline int gcd(int A, int B)
	{
		int Temp;

		if (A < B)
		{
			Temp = A;
			A = B;
			B = Temp;
		}

		while (B != 0)
		{
			Temp = A % B;
			A = B;
			B = Temp;
		}

		return A;
	}

	// Convert a fixed-point to a float.
	inline float MFOffsetToFloat(const MFOffset& offset)
	{
		return (float)offset.value + ((float)offset.value / 65536.0f);
	}

	inline RECT MFVideoAreaToRect(const MFVideoArea area)
	{
		float left = MFOffsetToFloat(area.OffsetX);
		float top = MFOffsetToFloat(area.OffsetY);

		RECT rc =
		{
			int(left + 0.5f),
			int(top + 0.5f),
			int(left + area.Area.cx + 0.5f),
			int(top + area.Area.cy + 0.5f)
		};

		return rc;
	}
	inline MFOffset MakeOffset(float v)
	{
		MFOffset offset;
		offset.value = short(v);
		offset.fract = WORD(65536 * (v - offset.value));
		return offset;
	}

	inline MFVideoArea MakeArea(float x, float y, DWORD width, DWORD height)
	{
		MFVideoArea area;
		area.OffsetX = MakeOffset(x);
		area.OffsetY = MakeOffset(y);
		area.Area.cx = width;
		area.Area.cy = height;
		return area;
	}
};

class LockedVideoMemoryBuffer
{
public:
	LockedVideoMemoryBuffer()
	{
		return;
	}
	UINT uiWidth = 0;
	UINT uiHeight = 0;
	BYTE* pByteBuffer = nullptr;
	LONG lStride = 0;
	BYTE* pByteBufferStart = nullptr;
	DWORD dwBufferLength = 0;

	int iBufferTypeUsed = 0;	//1 = IMF2dBuffer2, 2 = IMF2dBuffer, 3 = IMFMediaBuffer
	/// <summary>
	/// Call this function to Lock the memory buffer of the passed in Video MediaSample
	/// If S_OK returned then the memory buffer of the sample is locked and the public variables
	/// have been initialized with their appropriate values.
	/// Owner of this object must call Unlock() when finished processing the memory buffer of this sample
	/// </summary>
	/// <param name="p_pIMFMediaSample">Required. Must be a Video sample</param>
	/// <param name="p_LockFlags">Required</param>
	/// <param name="p_IMFMediaType">Optional. Must be provided only if IMFMediaBuffer is available</param>
	/// <returns></returns>
	HRESULT LockBuffer(IMFSample* p_pIMFMediaSample,
						MF2DBuffer_LockFlags p_LockFlags,
						IMFMediaType* p_pIMFMediaType)//optional value - may not be needed
	{
		HRESULT r_hrResult = S_FALSE;
		
		//These values will only be set of MF2DBuffer supported. Set to default values
		pByteBufferStart = nullptr;
		dwBufferLength = 0;

		//Get the media buffer from the sample. richfr to do which one to use?
		//HRESULT l_HResult = p_pIMFMediaSample->ConvertToContiguousBuffer(&m_pIMFMediaBuffer);
		HRESULT l_HResult = p_pIMFMediaSample->GetBufferByIndex(0, &m_pIMFMediaBuffer);
		//From the MediaBuffer, get the IMF2DBuffer 
		if (l_HResult == S_OK)
		{
			HRESULT hr = m_pIMFMediaBuffer->QueryInterface(IID_PPV_ARGS(&m_p2DBuffer2));
			if (hr == S_OK)
			{
				HRESULT l_hrLock = m_p2DBuffer2->Lock2DSize(p_LockFlags,//richfr what should this flag be?
					&pByteBuffer,
					&lStride,
					&pByteBufferStart,
					&dwBufferLength);
				if (l_hrLock == S_OK)
				{
					//we now have the pointer to the source buffer of this IMFSample along with its stride
					iBufferTypeUsed = 1;//2dBuffer2 used
					r_hrResult = S_OK;//return success back to the caller
				}
				else
				{
					r_hrResult = l_hrLock;
				}//failed to lock the 2dbuffer2
			}
			else
			{
				//There is no 2DBuffer2, so fallback to just 2DBuffer
				HRESULT l_hrGetBuff = m_pIMFMediaBuffer->QueryInterface(IID_PPV_ARGS(&m_p2DBuffer));
				if (l_hrGetBuff == S_OK)
				{
					//get all the details we need to copy from this buffer

					HRESULT l_hrLock = m_p2DBuffer->Lock2D(&pByteBuffer,
						&lStride);
					if (l_hrLock == S_OK)
					{
						//we now have the pointer to the source buffer of this IMFSample along with its stride
						iBufferTypeUsed = 2;//2DBuffer used
						r_hrResult = S_OK;
					}
					else
					{
						r_hrResult = l_hrLock;
					}//failed to lock the 2dbuffer
				}
				else
				{
					//No 2dBuffer, so just use the IMFMediaBuffer object itself
					//This requires the user passes in an IMFMediaType object. Did they?
					if (p_pIMFMediaType != nullptr)
					{
						HRESULT l_hrGetStride = GetDefaultStride(p_pIMFMediaType,
																	&lStride);
						if (l_hrGetStride == S_OK)
						{
							//We have the stride of the source video frame. Now get the Buffer pointer
							DWORD l_dwMaxLength;
							hr = m_pIMFMediaBuffer->Lock(&pByteBuffer,
								&l_dwMaxLength,
								&dwBufferLength);
							if (hr == S_OK)
							{
								/* richfr to do add this code
								 if (SUCCEEDED(hr))
								{
									*plStride = lDefaultStride;
									if (lDefaultStride < 0)
									{
										// Bottom-up orientation. Return a pointer to the start of the
										// last row *in memory* which is the top row of the image.
										*ppbScanLine0 = pData + abs(lDefaultStride) * (dwHeightInPixels - 1);
									}
									else
									{
										// Top-down orientation. Return a pointer to the start of the
										// buffer.
										*ppbScanLine0 = pData;
									}
								}*/
								//we now have the pointer to the source buffer of this IMFSample along with its stride
								iBufferTypeUsed = 3;	//IMFMediaBuffer used
								r_hrResult = S_OK;
							}
							else
							{
								//failed to lock the buffer
								r_hrResult = hr;
							};
						}
						else
						{
							//Unable to compute the stride
							r_hrResult = l_hrGetStride;
						}//we've tried everything and can't get the stride.
					}//we have the mediatype object
				}//failed to get 2DBuffer
			}//Failed to get 2dbuffer2
		}//we have the media buffer
		return r_hrResult;
	}

	/// <summary>
	/// Unlocks the buffer of the appropriate MediaBuffer type and releases all objects
	/// </summary>
	/// <returns></returns>
	HRESULT UnLockBuffer()
	{
		HRESULT r_hrReturn = S_FALSE;
		switch (iBufferTypeUsed)
		{
		case 0://buffer not locked
			//this is an error condition and means the buffer was never locked
			//However, since it is not locked, we can return success
			r_hrReturn = S_OK;
			break;
		case 1://2DBuffer2 was used
			r_hrReturn = m_p2DBuffer2->Unlock2D();
			break;
		case 2://2DBuffer was used
			r_hrReturn = m_p2DBuffer->Unlock2D();
			break;
		case 3://IMFMediaBuffer was used
			r_hrReturn = m_pIMFMediaBuffer->Unlock();
			break;
		default:
			//unexpected. this should not happen
			r_hrReturn = S_OK;
			break;
		}
		//richfr todo confirm this is the correct way to release a comptr object
		m_p2DBuffer2.ReleaseAndGetAddressOf();//richfr todo confirm this is the correct way to release a comptr object
		m_p2DBuffer.ReleaseAndGetAddressOf();
		m_pIMFMediaBuffer.ReleaseAndGetAddressOf();
		return r_hrReturn;
	}

private:
	//One of these will be used
	ComPtr<IMFMediaBuffer> m_pIMFMediaBuffer;
	ComPtr<IMF2DBuffer2> m_p2DBuffer2;
	ComPtr<IMF2DBuffer> m_p2DBuffer;

	HRESULT GetDefaultStride(IMFMediaType* pType, LONG* p_plStride)
	{
		HRESULT hr = S_FALSE;
		// Try to get the default stride from the media type.
		hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)p_plStride);
		if (FAILED(hr))
		{
			// Attribute not set. Try to calculate the default stride.

			GUID subtype = GUID_NULL;

			UINT32 width = 0;
			UINT32 height = 0;

			// Get the subtype and the image size.
			hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
			if (FAILED(hr))
			{
				goto done;
			}

			hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
			if (FAILED(hr))
			{
				goto done;
			}

			hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, p_plStride);
			if (FAILED(hr))
			{
				goto done;
			}
			//we got it
			// Set the attribute for later reference.
			(void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(*p_plStride));
			hr = S_OK;
		}
	done:
		return hr;
	}
};
class SampleGenerator
{
public:
	/*
	SampleGenerator(ComPtr<IMFSourceReader> p_pSourceReader)
	{
		m_pSourceReader = p_pSourceReader;
		return;
	}*/
	
	SampleGenerator(IMFDXGIDeviceManager* p_pIMFDXGIDeviceManager,
					DWORD *p_dwHeight,
					DWORD *p_dwWidth,
					HRESULT *p_pHRESULT)
	{
		*p_pHRESULT = S_FALSE;
		m_IMFDXGIDeviceManager = p_pIMFDXGIDeviceManager;

		*p_pHRESULT = CreateSourceReader(m_IMFDXGIDeviceManager.Get(),
										MEDIA_FILE_NAME,
										FALSE,
										MFVideoFormat_NV12, //MFVideoFormat_RGB32
										&m_pSourceReader,
										p_dwWidth,//this will contain the default size from the sourcereader
										p_dwHeight);
		if(SUCCEEDED(*p_pHRESULT))
		{
			*p_pHRESULT = SetSampleFormat(m_IMFDXGIDeviceManager.Get());
		}
		if (FAILED(*p_pHRESULT))
			LOG_HR_MSG(*p_pHRESULT, "SampleGenerator() failed");
		return;
	}
	
	HRESULT SetSampleFormat(IMFDXGIDeviceManager *p_pIMFDXGIDeviceManager)
	{
		HRESULT r_HResult = S_OK;
		ComPtr<IMFAttributes> l_pIMFAllocatorAttributes;
		ComPtr<IMFMediaType> l_pIMFInputMediaType;

		if (m_pSampleAllocator == nullptr)
		{
			//Create the sample Allocator
			r_HResult = MFCreateVideoSampleAllocatorEx(IID_IMFVideoSampleAllocatorEx, //this creates a dx11 allocator
														&m_pSampleAllocator);
		}
		else
		{
			//Sample allocator already exists, so un-initialize it
			r_HResult = m_pSampleAllocator->UninitializeSampleAllocator();
		}
		if (SUCCEEDED(r_HResult))
		{
			r_HResult = m_pSampleAllocator->SetDirectXManager(p_pIMFDXGIDeviceManager);
			if (SUCCEEDED(r_HResult))
			{
				r_HResult = MFCreateAttributes(&l_pIMFAllocatorAttributes, 1);
				if (SUCCEEDED(r_HResult))
				{
					r_HResult = l_pIMFAllocatorAttributes->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT);//richfr to do confirm the usage flag is correct
					if (SUCCEEDED(r_HResult))
					{
						r_HResult = l_pIMFAllocatorAttributes->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);//richfr todo is shader needed? | D3D11_BIND_SHADER_RESOURCE);
						if (SUCCEEDED(r_HResult))
						{

							r_HResult = GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,//this should be the video stream...richfr todo is this the best way?
															&l_pIMFInputMediaType);

							if (SUCCEEDED(r_HResult))
							{
								//Store the size for use by others
								m_uiVideoWidth = 0;
								m_uiVideoHeight = 0;
								MFGetAttributeSize(l_pIMFInputMediaType.Get(),//richfr todo, check for error here
									MF_MT_FRAME_SIZE,
									&m_uiVideoWidth,
									&m_uiVideoHeight);
								r_HResult = m_pSampleAllocator->InitializeSampleAllocatorEx(5,//richfr to do what should this value be
									20,//richfr todo ditto
									l_pIMFAllocatorAttributes.Get(),
									l_pIMFInputMediaType.Get());
								if (SUCCEEDED(r_HResult))
								{
									//All worked
									r_HResult = S_OK;
								}//Attrib and Type set
							}//Set the attributes and media type
						}//attrib set
					}//attrib set
				}//attribute created
			}//dxmanager setr
		}//sample allocated created/initialized
		//Proceed if the above worked
		if (SUCCEEDED(r_HResult))
		{
			//Next, Setup the VideoProcessor
			if (m_pVideoProcessorClass == nullptr)
			{
				//First Create it
				m_pVideoProcessorClass = new VideoProcessorClass();
				r_HResult = m_pVideoProcessorClass->Initialize(p_pIMFDXGIDeviceManager);//only call this 1x
			}
			if (SUCCEEDED(r_HResult))
			{
				//For now, Set the input and the output on the video processor to be the same
				HRESULT l_hrSetVPInput = m_pVideoProcessorClass->SetInputMediaType(l_pIMFInputMediaType.Get());
				if (SUCCEEDED(l_hrSetVPInput))
				{
					HRESULT l_hrSetVPOutput = m_pVideoProcessorClass->SetOutputMediaType(l_pIMFInputMediaType.Get());
					if (SUCCEEDED(l_hrSetVPOutput))
					{
						HRESULT l_hrStartVideoProc = m_pVideoProcessorClass->Start();
						if (SUCCEEDED(l_hrStartVideoProc))
						{
							r_HResult = S_OK;
						}
					}//we set the output format of the videoproc
				}//we set the input format of the videoproc
			}//videoproc object succesfully allocated
		}//Sample Allocator succesfully created or unititalized
		return r_HResult;
	}
	HRESULT GetStreamSelection(DWORD p_dwStreamIndexIn, BOOL* p_pbValidStream)
	{
		HRESULT r_hrResult = m_pSourceReader->GetStreamSelection(p_dwStreamIndexIn, p_pbValidStream);
		return r_hrResult;
	}
	HRESULT GetCurrentMediaType(DWORD p_dwStreamIndexIn, IMFMediaType **p_pIMFMediaType)
	{
		HRESULT r_hrResult = m_pSourceReader->GetCurrentMediaType(p_dwStreamIndexIn, p_pIMFMediaType);
		return r_hrResult;
	}
	HRESULT SetCurrentPosition(const GUID &p_GuidTimeFormat, const PROPVARIANT &p_StartTime)
	{
		HRESULT r_hrResult = m_pSourceReader->SetCurrentPosition(p_GuidTimeFormat, p_StartTime);
		return r_hrResult;
	}
	HRESULT ReadSample(DWORD p_dwSourceStreamId,
		GUID* p_pGuidMajorType,
		DWORD p_dwControlFlags,
		DWORD* p_pdwStreamIndex,
		DWORD* p_pdwFlags,
		LONGLONG* p_pllTimeStamps,
		IMFSample** p_ppIMFMediaSample)
	{
		IMFSample* l_pFromSrcReader = nullptr;
		IMFSample* l_pFromAllocator = nullptr;
		IMFSample* l_pFromVideoProc = nullptr;
		BOOL l_bFreeAllocSample = TRUE;	//we will set to this to false if we faile to get sample from VideoProc and will instead use the sample from the Allocator

		HRESULT r_hrResult = m_pSourceReader->ReadSample(p_dwSourceStreamId,
			p_dwControlFlags,
			p_pdwStreamIndex,
			p_pdwFlags,
			p_pllTimeStamps,
			&l_pFromSrcReader);//must release this when finished
		if (r_hrResult == S_OK)
		{
			//Make sure we goit back the correct stream
			if (p_dwSourceStreamId == *p_pdwStreamIndex)
			{
				//make sure we are not at EOS. If we are, sample == nullptr
				if (!(*p_pdwFlags & MF_SOURCE_READERF_ENDOFSTREAM))
				{
					r_hrResult = S_FALSE;	//set to true if below all works.
					//if this is a video buffer we want to copy it and return a new IMFSample based on GPU buffers

					//if (false)//skip this for now.
					if (*p_pGuidMajorType == MFMediaType_Video)
					{
						//GET SOURCE BUFFER Details
						BYTE* l_pByteSource = nullptr;
						LONG l_lSourceStride = 0;
						BYTE* l_pByteSourceBufferStart = nullptr;
						DWORD l_dwSourceBufferLength = 0;
						//This returns a locked buffer
						/* we replaced the below function call with a new function

						HRESULT l_hrGetBufferInfo = PullBufferInfoFromMediaSample(l_pFromSrcReader,
							*p_pdwStreamIndex,
							MF2DBuffer_LockFlags_Read,
							&l_pByteSource,
							&l_lSourceStride,
							&l_pByteSourceBufferStart,
							&l_dwSourceBufferLength);
						*/
						//See if a format change has occurred and if so, refresh our sample generators
						if (*p_pdwFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
						{
							//Yes, they have so get the frame size as this is currently all we support dynamically changing
							//Get the video output media type
							ComPtr<IMFMediaType> l_pIMFNewMediaType;

							HRESULT l_hrNewMediaType = GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,//this should be the video stream...richfr todo is this the best way?
								&l_pIMFNewMediaType);
							if (SUCCEEDED(l_hrNewMediaType))
							{
								//get the dimensions and see if different.
								UINT l_uiNewVideoWidth = 0;
								UINT l_uiNewVideoHeight = 0;
								HRESULT l_hrGetSize = MFGetAttributeSize(l_pIMFNewMediaType.Get(),
									MF_MT_FRAME_SIZE,
									&l_uiNewVideoWidth,
									&l_uiNewVideoHeight);
								if ((m_uiVideoWidth != l_uiNewVideoWidth) || (m_uiVideoHeight != l_uiNewVideoHeight))
								{
									HRESULT l_SetNewFormat = SetSampleFormat(m_IMFDXGIDeviceManager.Get());
									if (FAILED(l_SetNewFormat))
									{
										//log an error here richfr todo
									}//save the new format we just switched to
								}//dimensions of video frame coming from the decoder has changed
							}//We have the format of the new video type
						}//A video fromat has changed
						//Note, that even if the above video format changed happened, we continue to attempt to run regardless of whether we support
						//the format change or not.
						LockedVideoMemoryBuffer* l_pSourceLockedBuffer = new LockedVideoMemoryBuffer();

						HRESULT l_hrGetBufferInfo = l_pSourceLockedBuffer->LockBuffer(l_pFromSrcReader,
							MF2DBuffer_LockFlags_Read,
							nullptr);	//I don't have a media type to pass in.
						//RichFr ToDo, store the IMFMediaType from the source reader for this sample
						//Since IMF2dBuffer2 should work, this should not be needed.
						if (l_hrGetBufferInfo == S_OK)
						{
							//we now have all the pointer to the source buffer
							//Get a new MediaSample
							HRESULT l_hrGPUBuffer = CopyBufferToGPUBackedSample(l_pSourceLockedBuffer,
								l_pByteSource,
								l_lSourceStride,
								m_uiVideoWidth,
								m_uiVideoHeight,//not used
								&l_pFromAllocator);
							if (l_hrGPUBuffer == S_OK)
							{
								//we did it. The new MediaBuffer will be returned.
								//Unlock the original media buffer sample and free it
								HRESULT l_hrUnlock = l_pSourceLockedBuffer->UnLockBuffer();//richfr todo, in the unlockbuffer code, the int needs to be a read only value so user does not change it
								if (SUCCEEDED(l_hrUnlock))
								{
									//Copy the sample time and duration from the original IMFSample
									// to the new copied sample we are returning
									LONGLONG l_llSampleTime = 0;
									HRESULT l_hrGetSampleInfo = l_pFromSrcReader->GetSampleTime(&l_llSampleTime);
									if (SUCCEEDED(l_hrGetSampleInfo))
									{
										//Get Sample Duration
										LONGLONG l_llSampleDuration = 0;
										l_hrGetSampleInfo = l_pFromSrcReader->GetSampleDuration(&l_llSampleDuration);
										if (SUCCEEDED(l_hrGetSampleInfo))
										{
											//Set both these values
											HRESULT l_hrSetValue = l_pFromAllocator->SetSampleTime(l_llSampleTime);
											if (SUCCEEDED(l_hrSetValue))
											{
												l_hrSetValue = l_pFromAllocator->SetSampleDuration(l_llSampleDuration);
												//l_hrSetValue = l_pFromAllocator->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
												if (SUCCEEDED(l_hrSetValue))
												{

													//DEBUG RICHFR copy out all the attributes of the sample from the source reader
													// 
													/*
													IMFAttributes* l_pSrcAttributes = nullptr;
													IMFAttributes* l_pDestAttributes = nullptr;
													HRESULT hr = l_pFromSrcReader->QueryInterface(IID_PPV_ARGS(&l_pSrcAttributes));
													hr = l_pFromAllocator->QueryInterface(IID_PPV_ARGS(&l_pDestAttributes));
													hr = l_pSrcAttributes->CopyAllItems(l_pDestAttributes);
													//See what all the guids are
													if (SUCCEEDED(hr))
													{
														UINT32 count = 0;
														l_pSrcAttributes->GetCount(&count);

														for (UINT32 i = 0; i < count; i++) {
															GUID guid;
															PROPVARIANT l_PropVar;
															hr = l_pSrcAttributes->GetItemByIndex(i, &guid, &l_PropVar);
															// Handle the attribute based on its GUID
															GUID l_lCopy = guid;
														}
													}
													l_pDestAttributes->Release();
													l_pSrcAttributes->Release();
													DWORD l_dwSampleFlags = 0;
													hr = l_pFromSrcReader->GetSampleFlags(&l_dwSampleFlags);
													hr = l_pFromAllocator->SetSampleFlags(l_dwSampleFlags);
													*/

													//richfr as a debug test, take the buffer from source reader and copy the bits to the allocator using CopyTo().
													//this will overwrite the above code which did the copy using MFCopyImage.
													//r_hrResult = CopySampleBuffer(l_pFromSrcReader, l_pFromAllocator);
													//richfr As a debug test, take the new copied buffer in Allocator and copy it to source reader sample
													// and render the source reader sample.
													//r_hrResult = SwapBuffers(l_pFromSrcReader, l_pFromAllocator);
													//swap them back
													//r_hrResult = SwapBuffers(l_pFromSrcReader, l_pFromAllocator);

													//At this point, the allocator has the IMFMediaBuffer from the Source Reader
													// and vice versa. Now see if SourceReader sample still displays correctly.
													// 
													//Everything worked. We now have a GPU backed IMFSample created from the IMFMediaSample
													// passed in (or simply from a provided data buffer.
													// Now send it to the video processor for resizing and return that media sample.
													r_hrResult = m_pVideoProcessorClass->GetSample(l_pFromAllocator, //l_pFromAllocator,//l_pFromSrcReader, //as a test try the source reader sample l_pFromAllocator,//input allocator to copy image from
														&l_pFromVideoProc);//output allocator to place the image
													if (SUCCEEDED(r_hrResult))
													{
														*p_ppIMFMediaSample = l_pFromVideoProc;
														r_hrResult = S_OK;
													}//We got the sample from the VideoProc
													else
													{
														//it failed. In this case, we will just return the sample from the allocator
														l_bFreeAllocSample = FALSE;//since we are using the allocator's sample as a return value don't release it
														*p_ppIMFMediaSample = l_pFromAllocator;
														r_hrResult = S_OK;
													}

													//richfr to do. Note if we fail after ppIMFSample created, should we free it before returning the error?
												}//sample duration set
											}//sample time set
										}//got the sample duration
									}//got the sample time
								}//buffers succesfully unlocked
								else
								{
									r_hrResult = S_FALSE;
								}//Unable to unlock buffer.
								//We need to free the sample created by the Allocator if it is not being used
								if (l_bFreeAllocSample)
								{
									l_pFromAllocator->Release();
									l_pFromAllocator = nullptr;
								}//we are using the videoproc sample.
							}
						}//We read the buffer information from the sample
						else
						{
							//We were not able to get buffer info from the incoming mediasample
							r_hrResult = l_hrGetBufferInfo;
						}//unable to get buffer info from incoming mediasample
						//Release the MediaSample allocated by SourceReader. Ok to do that here because
						//the copy either worked and we are returning a new sample or it failed
						//and we are going to bale.
						l_pFromSrcReader->Release();//richfr todo do we need to release the buffers?
						l_pFromSrcReader = nullptr;
					}//if this is a video sample
					else
					{
						//this is not a video sample so we don't copy it. We just return it as is. Do not release the sample
						//since we are returning it for use.
						r_hrResult = S_OK;
						*p_ppIMFMediaSample = l_pFromSrcReader;
					}//not a video sample
				}//If not at end of stream
			}//if we got a sample from the correct stream
			else
			{
				r_hrResult == S_FALSE;
			}//sample returned from wrong stream
		}//able to read from sourcereader
		return r_hrResult;
	}
	
private:
	UINT m_uiVideoWidth = 0;
	UINT m_uiVideoHeight = 0;
	ComPtr<IMFDXGIDeviceManager> m_IMFDXGIDeviceManager;
	ComPtr< IMFVideoSampleAllocatorEx> m_pSampleAllocator;
	VideoProcessorClass* m_pVideoProcessorClass = nullptr;

	ComPtr<IMFSourceReader> m_pSourceReader;
	HRESULT CreateSourceReader(
		IUnknown* unkDeviceMgr,
		LPCWSTR fileURL,
		BOOL useSoftWareDecoder,
		GUID colorspace,
		IMFSourceReader** reader,
		DWORD* width,
		DWORD* height)
	{
		ComPtr<IMFSourceReader> sourceReader;
		ComPtr<IMFMediaType> partialTypeVideo;
		ComPtr<IMFMediaType> nativeMediaType;
		ComPtr<IMFAttributes> mfAttributes;

		if (unkDeviceMgr != nullptr)
		{
			RETURN_IF_FAILED(MFCreateAttributes(&mfAttributes, 1));
			RETURN_IF_FAILED(mfAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, unkDeviceMgr));
			if (useSoftWareDecoder)
			{
				RETURN_IF_FAILED(mfAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE));
				RETURN_IF_FAILED(mfAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE));
			}
			else
			{
				RETURN_IF_FAILED(mfAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
				RETURN_IF_FAILED(mfAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE));
			}
		}
		RETURN_IF_FAILED(MFCreateSourceReaderFromURL(fileURL, mfAttributes.Get(), &sourceReader));

		//richfr removedRETURN_IF_FAILED(sourceReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeMediaType));
		//richfr for debug purposes, try this.
		RETURN_IF_FAILED(sourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &nativeMediaType));

		RETURN_IF_FAILED(MFGetAttributeSize(nativeMediaType.Get(), MF_MT_FRAME_SIZE, (UINT*)width, (UINT*)height));

		// Set valid color space (default is MFVideoFormat_H264)
		RETURN_IF_FAILED(MFCreateMediaType(&partialTypeVideo));
		RETURN_IF_FAILED(partialTypeVideo->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));

		RETURN_IF_FAILED(partialTypeVideo->SetGUID(MF_MT_SUBTYPE, colorspace));
		RETURN_IF_FAILED(sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, partialTypeVideo.Get()));


		*reader = sourceReader.Detach();

		return S_OK;
	}
	/// <summary>
	/// This function will take the passed in MediaSample and Lock its buffer and return the values from it needed
	/// to copy to our from it. This function will lock the buffer using the lock flag passed in. It is
	/// up to the caller to unlock this mediabuffer sample when done using the buffer.
	/// Note:
	/// p_LockFlags is only used if the buffer passed in supports IMF2DBuffer interface
	/// p_bByteBufferStart will only be valid if the buffer passed in supports IMF2DBuffer interface. nullptr returned otherwise.
	/// p_pdwBufferLength  will only be valid if the buffer passed in supports IMF2DBuffer interface. 0 returned otherwise.
	/// </summary>
	/// <param name="p_pIMFMediaSample"></param>
	/// <param name="p_pByteBuffer"></param>
	/// <param name="p_pStride"></param>
	/// <param name="p_pByteBufferStart"></param>
	/// <param name="p_pdwBufferLength"></param>
	/// <returns></returns>
	HRESULT PullBufferInfoFromMediaSample(IMFSample* p_pIMFMediaSample,
											DWORD p_dwStreamID,//StreamID this media sample came from.
											MF2DBuffer_LockFlags p_LockFlags,
											BYTE** p_pByteBuffer,
											LONG* p_plStride,
											BYTE** p_pByteBufferStart,
											DWORD* p_pdwBufferLength)
	{
		HRESULT r_hrResult = S_FALSE;
		IMFMediaBuffer* l_pIMFMediaBuffer = NULL;

		//These values will only be set of MF2DBuffer supported. Set to default values
		*p_pByteBufferStart = nullptr;
		*p_pdwBufferLength = 0;
		
		//now copy the memory buffer of the just read IMFSample into a new GPUBacked IMFSample and return that sample
		HRESULT l_HResult = p_pIMFMediaSample->GetBufferByIndex(0, &l_pIMFMediaBuffer);
		//From the MediaBuffer, get the IMF2DBuffer 
		if (l_HResult == S_OK)
		{
			IMF2DBuffer2* l_p2DBuffer2 = nullptr;
			HRESULT hr = l_pIMFMediaBuffer->QueryInterface(IID_PPV_ARGS(&l_p2DBuffer2));
			if (hr == S_OK)
			{
				HRESULT l_hrLock = l_p2DBuffer2->Lock2DSize(p_LockFlags,//richfr what should this flag be?
					p_pByteBuffer,
					p_plStride,
					p_pByteBufferStart,
					p_pdwBufferLength);
				if (l_hrLock == S_OK)
				{
					//we now have the pointer to the source buffer of this IMFSample along with its stride
					r_hrResult = S_OK;//return success back to the caller
				}
				else
				{
					r_hrResult = l_hrLock;
				}//failed to lock the 2dbuffer2
			}
			else
			{
				//There is no 2DBuffer2, so fallback to just 2DBuffer
				IMF2DBuffer* l_p2DBuffer = nullptr;
				HRESULT hr = l_pIMFMediaBuffer->QueryInterface(IID_PPV_ARGS(&l_p2DBuffer));
				if (hr == S_OK)
				{
					//get all the details we need to copy from this buffer
					
					HRESULT l_hrLock = l_p2DBuffer->Lock2D(p_pByteBuffer,
															p_plStride);
					if (l_hrLock == S_OK)
					{
						//we now have the pointer to the source buffer of this IMFSample along with its stride
					}
					else
					{
						r_hrResult = l_hrLock;
					}//failed to lock the 2dbuffer
				}
				else
				{
					//No 2dBuffer, so just use the IMFMediaBuffer object itself
					IMFMediaType* l_pSrcIMFMediaType = nullptr;
					HRESULT l_hrGetMediaType = m_pSourceReader->GetCurrentMediaType(p_dwStreamID,
																					&l_pSrcIMFMediaType);
					if (l_hrGetMediaType == S_OK)
					{
						HRESULT l_hrGetStride = GetDefaultStride(l_pSrcIMFMediaType,
																	p_plStride);
						if (l_hrGetStride == S_OK)
						{
							//We have the stride of the source video frame. Now get the Buffer pointer
							DWORD l_dwMaxLength;
							hr = l_pIMFMediaBuffer->Lock(p_pByteBuffer,
															&l_dwMaxLength,
															p_pdwBufferLength);
							if (hr == S_OK)
							{
								//we now have the pointer to the source buffer of this IMFSample along with its stride
								
							}
							else
							{
								//failed to lock the buffer
							};
						}
						else
						{
							//Unable to compute the stride
							r_hrResult = l_hrGetStride;
						}//we've tried everything and can't get the stride.
					}//we have the mediatype object
				}//failed to get 2DBuffer
			}//Failed to get 2dbuffer2
		}//we have the media buffer
		return r_hrResult;
	}

	/// <summary>
	/// Swap the mediabuffers between the two samples. This is just debugging code.
	/// </summary>
	/// <param name="p_pIMFSample1"></param>
	/// <param name="p_pIMFSample2"></param>
	/// <returns></returns>
	HRESULT SwapBuffers(IMFSample* p_pIMFSample1,
		IMFSample* p_pIMFSample2)
	{
		IMFMediaBuffer *l_pIMFMediaBuffer1;
		IMFMediaBuffer *l_pIMFMediaBuffer2;

		HRESULT r_HResult = S_OK;
		
		

		HRESULT l_HResult1 = p_pIMFSample1->GetBufferByIndex(0, &l_pIMFMediaBuffer1);//returns a referenced object of 1
		HRESULT l_HResult2 = p_pIMFSample2->GetBufferByIndex(0, &l_pIMFMediaBuffer2);//returns a referenced object""
		l_HResult1 = p_pIMFSample1->RemoveBufferByIndex(0);//this does not change the ref count
		l_HResult2 = p_pIMFSample2->RemoveBufferByIndex(0);
		l_HResult1 = p_pIMFSample1->AddBuffer(l_pIMFMediaBuffer2);//this adds a ref count so now we are at 2
		l_pIMFMediaBuffer2->Release();
		l_HResult2 = p_pIMFSample2->AddBuffer(l_pIMFMediaBuffer1);//this adds a ref count so now we are at 2
		l_pIMFMediaBuffer1->Release();
		return r_HResult;
	}
	/// <summary>
	/// richfr this is debug code. On return from this fuctnion, Sample2 will have a copy of the buffer that was in
	/// sample1.
	/// </summary>
	/// <param name="p_pIMFSample1"></param>
	/// <param name="p_pIMFSample2"></param>
	/// <returns></returns>
	HRESULT CopySampleBuffer(IMFSample* p_pIMFSample1,
		IMFSample* p_pIMFSample2)
	{
		IMFMediaBuffer* l_pIMFMediaBuffer1;
		IMFMediaBuffer* l_pIMFMediaBuffer2;

		HRESULT r_HResult = S_OK;
		//HRESULT l_HResult1 = p_pIMFSample1->GetBufferByIndex(0, &l_pIMFMediaBuffer1);//returns a referenced object of 1
		HRESULT l_HResult2 = p_pIMFSample2->GetBufferByIndex(0, &l_pIMFMediaBuffer2);//returns a referenced object""
		
		//Copy the source reader buffer contents to the buffer in our sample
		r_HResult = p_pIMFSample1->CopyToBuffer(l_pIMFMediaBuffer2);

		return r_HResult;
	}
	/// <summary>
	/// Pass in a system memory buffer that points to an image and we will return an IMFSample object backed by
	/// GPU memory that contains the image passed in.
	/// This function performs a copy of the image. The buffers passed in for the source must already be locked
	/// This does not unlock the source buffers passed in.
	/// </summary>
	/// <returns></returns>
	HRESULT CopyBufferToGPUBackedSample(LockedVideoMemoryBuffer* p_pSourceLockedBuffer,
										BYTE *p_pByteSourceImage,	//pointer to system memory of source image
										LONG p_lSourceStride,		//Stride of source image
										DWORD p_dwWidthInBytes,		//Width in bytes of source image
										DWORD p_dwLines,			//Number of lines of source image
										IMFSample** p_pIMFMediaSample)	//return GPU backed MediaSample of that image
	{
		HRESULT r_hrResult = S_FALSE;	//set to s_ok if we succeed
		BYTE* l_pByteDest = nullptr;	//pointer to the GPU buffer we want to copy into
		LONG l_lDestStride = 0;
		BYTE* l_pByteDestBufferStart = nullptr;
		DWORD l_dwDestBufferLength = 0;

		UINT l_dwVideoHeight = m_uiVideoHeight;// +8;//richfr debug test
		UINT l_dwVideoWidth = m_uiVideoWidth;

		//create the sample to hold the results
		HRESULT l_hrAllocate = m_pSampleAllocator->AllocateSample(p_pIMFMediaSample);
		//richfr todo when to use D3D11_USAGE_STAGING  when allocating memory buffer?
		//richfr todo: allocate the buffer for the above created media sample!
		if (l_hrAllocate == S_OK)
		{


			//Now, get the buffer information from the destination buffer
			LockedVideoMemoryBuffer* l_pLockedDestBuffer = new LockedVideoMemoryBuffer();
			HRESULT l_hrDestInfo = l_pLockedDestBuffer->LockBuffer(*p_pIMFMediaSample,
				MF2DBuffer_LockFlags_Write,
				nullptr);//richfr todo, get a mediatype for this sample to use here.
			/* below code replaced by the above
			HRESULT l_hrDestInfo = PullBufferInfoFromMediaSample(p_pIMFMediaSample,
				0,//richr note, this value not valid here. This should be the value of the video stream. But, this shoujld also not get used if gpu buffer.
				MF2DBuffer_LockFlags_Write,
				&l_pByteDest,
				&l_lDestStride,
				&l_pByteDestBufferStart,
				&l_dwDestBufferLength);
			*/
			if (l_hrDestInfo == S_OK)
			{
			
				//Copy the image from the source buffer to the dest buffer
				/*
				r_hrResult = MFCopyImage(l_pLockedDestBuffer->pByteBuffer,
					l_pLockedDestBuffer->lStride,
					p_pSourceLockedBuffer->pByteBuffer,
					p_pSourceLockedBuffer->lStride,
					m_uiVideoWidth,
					m_uiVideoHeight);
				*/
				//m_uiVideoHeight += 8;
				//copy each plane seperately
				// Copy Luma plan			
				r_hrResult = MFCopyImage(l_pLockedDestBuffer->pByteBuffer,
											l_pLockedDestBuffer->lStride,
											p_pSourceLockedBuffer->pByteBuffer,
											p_pSourceLockedBuffer->lStride,
											l_dwVideoWidth,
											l_dwVideoHeight);// +(m_uiVideoHeight / 2));

				//move to the end of the Luma buffer.
				//BYTE* l_pByteDestChroma = l_pLockedDestBuffer->pByteBuffer + l_pLockedDestBuffer->lStride * m_uiVideoHeight;
				//BYTE* l_pByteSrcChroma = p_pSourceLockedBuffer->pByteBuffer + p_pSourceLockedBuffer->lStride * m_uiVideoHeight;
				BYTE* l_pByteDestChroma = l_pLockedDestBuffer->pByteBuffer + (l_pLockedDestBuffer->lStride * (l_dwVideoHeight));//(m_uiVideoHeight - 8)
				BYTE* l_pByteSrcChroma = p_pSourceLockedBuffer->pByteBuffer + (p_pSourceLockedBuffer->lStride * (l_dwVideoHeight));// +8));//this works too

				
				// Copy Chroma plane
				r_hrResult = MFCopyImage(l_pByteDestChroma,
					l_pLockedDestBuffer->lStride,
					l_pByteSrcChroma,
					p_pSourceLockedBuffer->lStride,
					l_dwVideoWidth,
					(l_dwVideoHeight / 2));//(height / 2 * width / 2)
				//Unlock the DestBuffer
				//m_uiVideoHeight -= 8;
				r_hrResult = l_pLockedDestBuffer->UnLockBuffer();
			}
		}
		else
		{
			//Unable to allocate a sample
			r_hrResult = l_hrAllocate;
		}
		return r_hrResult;
	}
	HRESULT GetDefaultStride(IMFMediaType* pType, LONG* p_plStride)
	{
		HRESULT hr = S_FALSE;
		// Try to get the default stride from the media type.
		hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)p_plStride);
		if (FAILED(hr))
		{
			// Attribute not set. Try to calculate the default stride.

			GUID subtype = GUID_NULL;

			UINT32 width = 0;
			UINT32 height = 0;

			// Get the subtype and the image size.
			hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
			if (FAILED(hr))
			{
				goto done;
			}

			hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
			if (FAILED(hr))
			{
				goto done;
			}

			hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, p_plStride);
			if (FAILED(hr))
			{
				goto done;
			}
			//we got it
			// Set the attribute for later reference.
			(void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(*p_plStride));
			hr = S_OK;
		}
	done:
		return hr;
	}

};

