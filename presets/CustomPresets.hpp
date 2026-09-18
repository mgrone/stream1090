/*
Preset<IQ_INT16_ANTSDR, Sampler_6_0_to_12_0_Mhz, IQPipelineOptions::NONE>{},
Preset<IQ_INT16_ANTSDR, Sampler_6_0_to_12_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR>{},
Preset<IQ_INT16_ANTSDR, Sampler_6_0_to_24_0_Mhz, IQPipelineOptions::NONE>{},
Preset<IQ_INT16_ANTSDR, Sampler_6_0_to_24_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR>{},
*/

ADD_PRESET(Signed_12bit, 6, 12, None)
ADD_PRESET(Signed_12bit, 6, 12, Internal)

ADD_PRESET(Signed_12bit, 6, 24, None)
ADD_PRESET(Signed_12bit, 6, 24, Internal)

ADD_PRESET(Signed_12bit, 8, 24, None)
ADD_PRESET(Signed_12bit, 8, 24, Internal)
ADD_PRESET(Signed_12bit, 8, 24, File)

ADD_PRESET(Signed_12bit, 12, 24, None)
ADD_PRESET(Signed_12bit, 12, 24, Internal)
ADD_PRESET(Signed_12bit, 12, 24, File)

ADD_PRESET(Signed_12bit, 24, 24, None)
ADD_PRESET(Signed_12bit, 24, 24, Internal)
ADD_PRESET(Signed_12bit, 24, 24, File)


/*ADD_PRESET(Signed_12bit, 8, 12, Internal)
ADD_PRESET(RtlSdr, 2.56, 16, File)*/
ADD_PRESET(Signed_12bit, 2.33, 16, None)
