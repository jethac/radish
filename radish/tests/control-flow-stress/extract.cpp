#include <string>
#include <sstream>
#include <stack>
#include <fstream>
#include <iostream>
using std::string;
std::string getComputeAsmCommonTypes(std::string blockStorageClass = "Uniform");
std::string getComputeAsmInputOutputBuffer(std::string blockStorageClass = "Uniform");
std::string getComputeAsmInputOutputBufferTraits(std::string blockStorageClass = "BufferBlock");
string generateConstantDefinitions(int count)
{
    std::ostringstream r;
    for (int i = 0; i < count; i++)
        r << "%cf" << (i * 10 + 5) << " = OpConstant %f32 " << (i * 10 + 5) << ".0\n";
    r << "\n";
    return r.str();
}

string generateSwitchCases(int count)
{
    std::ostringstream r;
    for (int i = 0; i < count; i++)
        r << " " << i << " %case" << i;
    r << "\n";
    return r.str();
}

string generateSwitchTargets(int count)
{
    std::ostringstream r;
    for (int i = 0; i < count; i++)
        r << "%case" << i << " = OpLabel\n            OpBranch %phi\n";
    r << "\n";
    return r.str();
}

string generateOpPhiParams(int count)
{
    std::ostringstream r;
    for (int i = 0; i < count; i++)
        r << " %cf" << (i * 10 + 5) << " %case" << i;
    r << "\n";
    return r.str();
}

string generateIntWidth(int value)
{
    std::ostringstream r;
    r << value;
    return r.str();
}

string expandOpPhiCase5(const string &s, int &acc, int add, int treshold)
{
    std::ostringstream res;
    const char *p = s.c_str();

    while (*p)
    {
        res << *p;
        acc += add;
        if (acc > treshold)
        {
            acc -= treshold;
            res << "ABC";
        }
        p++;
    }
    return res.str();
}

string generateOpPhiCase5(const string &s)
{
    std::stack<int> idStack;
    std::stack<std::string> value;
    std::stack<std::string> valueLabel;
    std::stack<std::string> mergeLeft;
    std::stack<std::string> mergeRight;
    std::ostringstream res;
    const char *p = s.c_str();
    int depth     = -1;
    int currId    = 0;
    int iter      = 0;

    idStack.push(-1);
    value.push("%f32_0");
    valueLabel.push("%f32_0 %entry");

    while (*p)
    {
        if (*p == 'A')
        {
            depth++;
            currId = iter;
            idStack.push(currId);
            res << "\tOpSelectionMerge %m" << currId << " None\n";
            res << "\tOpBranchConditional %b" << depth << " %t" << currId << " %f" << currId << "\n";
            res << "%t" << currId << " = OpLabel\n";
            res << "%rt" << currId << " = OpFAdd %f32 " << value.top() << " %f32_1\n";
            std::ostringstream tag;
            tag << "%rt" << currId;
            value.push(tag.str());
            tag << " %t" << currId;
            valueLabel.push(tag.str());
        }

        if (*p == 'B')
        {
            mergeLeft.push(valueLabel.top());
            value.pop();
            valueLabel.pop();
            res << "\tOpBranch %m" << currId << "\n";
            res << "%f" << currId << " = OpLabel\n";
            std::ostringstream tag;
            tag << value.top() << " %f" << currId;
            valueLabel.pop();
            valueLabel.push(tag.str());
        }

        if (*p == 'C')
        {
            mergeRight.push(valueLabel.top());
            res << "\tOpBranch %m" << currId << "\n";
            res << "%m" << currId << " = OpLabel\n";
            if (*(p + 1) == 0)
                res << "%res"; // last result goes to %res
            else
                res << "%rm" << currId;
            res << " = OpPhi %f32  " << mergeLeft.top() << "  " << mergeRight.top() << "\n";
            std::ostringstream tag;
            tag << "%rm" << currId;
            value.pop();
            value.push(tag.str());
            tag << " %m" << currId;
            valueLabel.pop();
            valueLabel.push(tag.str());
            mergeLeft.pop();
            mergeRight.pop();
            depth--;
            idStack.pop();
            currId = idStack.top();
        }
        p++;
        iter++;
    }
    return res.str();
}

std::string getComputeAsmCommonTypes(std::string blockStorageClass)
{
    return std::string("%bool      = OpTypeBool\n"
                       "%void      = OpTypeVoid\n"
                       "%voidf     = OpTypeFunction %void\n"
                       "%u32       = OpTypeInt 32 0\n"
                       "%i32       = OpTypeInt 32 1\n"
                       "%f32       = OpTypeFloat 32\n"
                       "%uvec3     = OpTypeVector %u32 3\n"
                       "%fvec3     = OpTypeVector %f32 3\n"
                       "%uvec3ptr  = OpTypePointer Input %uvec3\n") +
           "%i32ptr    = OpTypePointer " + blockStorageClass +
           " %i32\n"
           "%f32ptr    = OpTypePointer " +
           blockStorageClass +
           " %f32\n"
           "%i32arr    = OpTypeRuntimeArray %i32\n"
           "%f32arr    = OpTypeRuntimeArray %f32\n";
}

std::string getComputeAsmInputOutputBuffer(std::string blockStorageClass)
{ // Uniform | StorageBuffer
    return std::string() +
           "%buf     = OpTypeStruct %f32arr\n"
           "%bufptr  = OpTypePointer " +
           blockStorageClass +
           " %buf\n"
           "%indata    = OpVariable %bufptr " +
           blockStorageClass +
           "\n"
           "%outdata   = OpVariable %bufptr " +
           blockStorageClass + "\n";
}

std::string getComputeAsmInputOutputBufferTraits(std::string blockStorageClass)
{ // BufferBlock | Block
    return std::string() + "OpDecorate %buf " + blockStorageClass +
           "\n"
           "OpDecorate %indata DescriptorSet 0\n"
           "OpDecorate %indata Binding 0\n"
           "OpDecorate %outdata DescriptorSet 0\n"
           "OpDecorate %outdata Binding 1\n"
           "OpDecorate %f32arr ArrayStride 4\n"
           "OpMemberDecorate %buf 0 Offset 0\n";
}
int main(int argc,char** argv) { if(argc!=2)return 2; const int test4Width=512; std::string codestring="ABC";
for (int i = 0, acc = 0; i < 9; i++) codestring = expandOpPhiCase5(codestring, acc, 7, 24);
std::string wide="OpCapability Shader\n"
                     "%ext = OpExtInstImport \"GLSL.std.450\"\n"
                     "OpMemoryModel Logical GLSL450\n"
                     "OpEntryPoint GLCompute %main \"main\" %id\n"
                     "OpExecutionMode %main LocalSize 1 1 1\n"

                     "OpSource GLSL 430\n"
                     "OpName %main \"main\"\n"
                     "OpName %id \"gl_GlobalInvocationID\"\n"

                     "OpDecorate %id BuiltIn GlobalInvocationId\n"

                     + string(getComputeAsmInputOutputBufferTraits()) + string(getComputeAsmCommonTypes()) +
                     string(getComputeAsmInputOutputBuffer()) +

                     "%id       = OpVariable %uvec3ptr Input\n"
                     "%zero     = OpConstant %i32 0\n"
                     "%cimod    = OpConstant %u32 " +
                     generateIntWidth(test4Width) + "\n"

                     + generateConstantDefinitions(test4Width) +

                     "%main     = OpFunction %void None %voidf\n"
                     "%entry    = OpLabel\n"
                     "%idval    = OpLoad %uvec3 %id\n"
                     "%x        = OpCompositeExtract %u32 %idval 0\n"
                     "%inloc    = OpAccessChain %f32ptr %indata %zero %x\n"
                     "%inval    = OpLoad %f32 %inloc\n"
                     "%xf       = OpConvertUToF %f32 %x\n"
                     "%xm       = OpFMul %f32 %xf %inval\n"
                     "%xa       = OpExtInst %f32 %ext FAbs %xm\n"
                     "%xi       = OpConvertFToU %u32 %xa\n"
                     "%selector = OpUMod %u32 %xi %cimod\n"
                     "            OpSelectionMerge %phi None\n"
                     "            OpSwitch %selector %default "

                     + generateSwitchCases(test4Width) +

                     "%default  = OpLabel\n"
                     "            OpUnreachable\n"

                     + generateSwitchTargets(test4Width) +

                     "%phi      = OpLabel\n"
                     "%result   = OpPhi %f32"

                     + generateOpPhiParams(test4Width) +

                     "%outloc   = OpAccessChain %f32ptr %outdata %zero %x\n"
                     "            OpStore %outloc %result\n"
                     "            OpReturn\n"

                     "            OpFunctionEnd\n";
std::string nested="OpCapability Shader\n"
                     "%ext      = OpExtInstImport \"GLSL.std.450\"\n"
                     "OpMemoryModel Logical GLSL450\n"
                     "OpEntryPoint GLCompute %main \"main\" %id\n"
                     "OpExecutionMode %main LocalSize 1 1 1\n"
                     "%code     = OpString \"" +
                     codestring +
                     "\"\n"

                     "OpSource GLSL 430\n"
                     "OpName %main \"main\"\n"
                     "OpName %id \"gl_GlobalInvocationID\"\n"

                     "OpDecorate %id BuiltIn GlobalInvocationId\n"

                     + string(getComputeAsmInputOutputBufferTraits()) + string(getComputeAsmCommonTypes()) +
                     string(getComputeAsmInputOutputBuffer()) +

                     "%id       = OpVariable %uvec3ptr Input\n"
                     "%zero     = OpConstant %i32 0\n"
                     "%f32_0    = OpConstant %f32 0.0\n"
                     "%f32_0_5  = OpConstant %f32 0.5\n"
                     "%f32_1    = OpConstant %f32 1.0\n"
                     "%f32_1_5  = OpConstant %f32 1.5\n"
                     "%f32_2    = OpConstant %f32 2.0\n"
                     "%f32_3_5  = OpConstant %f32 3.5\n"
                     "%f32_4    = OpConstant %f32 4.0\n"
                     "%f32_7_5  = OpConstant %f32 7.5\n"
                     "%f32_8    = OpConstant %f32 8.0\n"
                     "%f32_15_5 = OpConstant %f32 15.5\n"
                     "%f32_16   = OpConstant %f32 16.0\n"
                     "%f32_31_5 = OpConstant %f32 31.5\n"
                     "%f32_32   = OpConstant %f32 32.0\n"
                     "%f32_63_5 = OpConstant %f32 63.5\n"
                     "%f32_64   = OpConstant %f32 64.0\n"
                     "%f32_127_5 = OpConstant %f32 127.5\n"
                     "%f32_128  = OpConstant %f32 128.0\n"
                     "%f32_256  = OpConstant %f32 256.0\n"

                     "%main     = OpFunction %void None %voidf\n"
                     "%entry    = OpLabel\n"
                     "%idval    = OpLoad %uvec3 %id\n"
                     "%x        = OpCompositeExtract %u32 %idval 0\n"
                     "%inloc    = OpAccessChain %f32ptr %indata %zero %x\n"
                     "%inval    = OpLoad %f32 %inloc\n"

                     "%xabs     = OpExtInst %f32 %ext FAbs %inval\n"
                     "%x8       = OpFMod %f32 %xabs %f32_256\n"
                     "%x7       = OpFMod %f32 %xabs %f32_128\n"
                     "%x6       = OpFMod %f32 %xabs %f32_64\n"
                     "%x5       = OpFMod %f32 %xabs %f32_32\n"
                     "%x4       = OpFMod %f32 %xabs %f32_16\n"
                     "%x3       = OpFMod %f32 %xabs %f32_8\n"
                     "%x2       = OpFMod %f32 %xabs %f32_4\n"
                     "%x1       = OpFMod %f32 %xabs %f32_2\n"

                     "%b7       = OpFOrdGreaterThanEqual %bool %x8 %f32_127_5\n"
                     "%b6       = OpFOrdGreaterThanEqual %bool %x7 %f32_63_5\n"
                     "%b5       = OpFOrdGreaterThanEqual %bool %x6 %f32_31_5\n"
                     "%b4       = OpFOrdGreaterThanEqual %bool %x5 %f32_15_5\n"
                     "%b3       = OpFOrdGreaterThanEqual %bool %x4 %f32_7_5\n"
                     "%b2       = OpFOrdGreaterThanEqual %bool %x3 %f32_3_5\n"
                     "%b1       = OpFOrdGreaterThanEqual %bool %x2 %f32_1_5\n"
                     "%b0       = OpFOrdGreaterThanEqual %bool %x1 %f32_0_5\n"

                     + generateOpPhiCase5(codestring) +

                     "%outloc   = OpAccessChain %f32ptr %outdata %zero %x\n"
                     "            OpStore %outloc %res\n"
                     "            OpReturn\n"

                     "            OpFunctionEnd\n";
std::ofstream(std::string(argv[1])+"/wide.spvasm") << wide;
std::ofstream(std::string(argv[1])+"/nested.spvasm") << nested;
std::ofstream(std::string(argv[1])+"/nested-code.txt") << codestring << "\n";
return 0; }
