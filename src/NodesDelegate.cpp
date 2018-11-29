// https://github.com/CedricGuillemet/Imogen
//
// The MIT License(MIT)
// 
// Copyright(c) 2018 Cedric Guillemet
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "Nodes.h"
#include "Evaluation.h"
#include "ImCurveEdit.h"
#include "ImGradient.h"
#include "Library.h"
#include "nfd.h"
#include "EvaluationContext.h"
#include "NodesDelegate.h"


struct UndoRedoParameterBlock : public UndoRedo
{
	UndoRedoParameterBlock(size_t target, const std::vector<unsigned char>& preDo) : mTarget(target), mPreDo(preDo) {}
	virtual ~UndoRedoParameterBlock() {}
	virtual void Undo();
	virtual void Redo();
	std::vector<unsigned char> mPreDo;
	std::vector<unsigned char> mPostDo;
	size_t mTarget;
};

struct UndoRedoInputSampler : public UndoRedo
{
	UndoRedoInputSampler(size_t target, const std::vector<InputSampler>& preDo) : mTarget(target), mPreDo(preDo) {}
	virtual ~UndoRedoInputSampler() {}
	virtual void Undo();
	virtual void Redo();
	std::vector<InputSampler> mPreDo;
	std::vector<InputSampler> mPostDo;
	size_t mTarget;
};

void UndoRedoParameterBlock::Undo()
{
	gNodeDelegate.SetParamBlock(mTarget, mPreDo);
	gEvaluation.SetEvaluationParameters(mTarget, mPreDo);
	gCurrentContext->SetTargetDirty(mTarget);
}

void UndoRedoParameterBlock::Redo()
{
	gNodeDelegate.SetParamBlock(mTarget, mPostDo);
	gEvaluation.SetEvaluationParameters(mTarget, mPostDo);
	gCurrentContext->SetTargetDirty(mTarget);
}

void UndoRedoInputSampler::Undo()
{
	gNodeDelegate.mNodes[mTarget].mInputSamplers = mPreDo;
	gEvaluation.SetEvaluationSampler(mTarget, mPreDo);
	gCurrentContext->SetTargetDirty(mTarget);
}

void UndoRedoInputSampler::Redo()
{
	gNodeDelegate.mNodes[mTarget].mInputSamplers = mPostDo;
	gEvaluation.SetEvaluationSampler(mTarget, mPostDo);
	gCurrentContext->SetTargetDirty(mTarget);
}

TileNodeEditGraphDelegate gNodeDelegate;
TileNodeEditGraphDelegate::TileNodeEditGraphDelegate() : mbMouseDragging(false), mEditingContext(gEvaluation, false, 1024, 1024)
{
	mCategoriesCount = 9;
	static const char *categories[] = {
		"Transform",
		"Generator",
		"Material",
		"Blend",
		"Filter",
		"Noise",
		"File",
		"Paint",
		"Cubemap"};
	mCategories = categories;
	gCurrentContext = &mEditingContext;
}

void TileNodeEditGraphDelegate::Clear()
{
	mSelectedNodeIndex = -1;
	mNodes.clear();
}

void TileNodeEditGraphDelegate::SetParamBlock(size_t index, const std::vector<unsigned char>& parameters)
{
	ImogenNode & node = mNodes[index];
	node.mParameters = parameters;
	gEvaluation.SetEvaluationParameters(node.gEvaluationTarget, parameters);
	gEvaluation.SetEvaluationSampler(node.gEvaluationTarget, node.mInputSamplers);
}

void TileNodeEditGraphDelegate::AddNode(size_t type)
{
	const size_t index			= mNodes.size();
	const size_t paramsSize		= ComputeNodeParametersSize(type);
	const size_t inputCount		= gMetaNodes[type].mInputs.size();

	ImogenNode node;
	node.gEvaluationTarget		= gEvaluation.AddEvaluation(type, gMetaNodes[type].mName);
	node.mRuntimeUniqueId		= GetRuntimeId();
	node.mType					= type;
	node.mStartFrame			= 0;
	node.mEndFrame				= 0;
#ifdef _DEBUG
	node.mNodeTypename			= gMetaNodes[type].mName;
#endif
	node.mParameters.resize(paramsSize, 0);
	node.mInputSamplers.resize(inputCount);
	mNodes.push_back(node);

	gEvaluation.SetEvaluationParameters(node.gEvaluationTarget, node.mParameters);
	gEvaluation.SetEvaluationSampler(node.gEvaluationTarget, node.mInputSamplers);
}

void TileNodeEditGraphDelegate::DeleteNode(size_t index)
{
	gEvaluation.DelEvaluationTarget(mNodes[index].gEvaluationTarget);
	mNodes.erase(mNodes.begin() + index);
	for (auto& node : mNodes)
	{
		if (node.gEvaluationTarget > index)
			node.gEvaluationTarget--;
	}
	mEditingContext.RunAll();
}
	
const float PI = 3.14159f;
float RadToDeg(float a) { return a * 180.f / PI; }
float DegToRad(float a) { return a / 180.f * PI; }
void TileNodeEditGraphDelegate::EditNode()
{
	size_t index = mSelectedNodeIndex;

	const MetaNode* metaNodes = gMetaNodes.data();
	bool dirty = false;
	bool forceEval = false;
	bool samplerDirty = false;
	ImogenNode& node = mNodes[index];
	const MetaNode& currentMeta = metaNodes[node.mType];
		
	if (ImGui::CollapsingHeader("Samplers", 0))
	{
		UndoRedoInputSampler undoRedoInputSampler(index, node.mInputSamplers);
			
		for (size_t i = 0; i < node.mInputSamplers.size();i++)
		{
			InputSampler& inputSampler = node.mInputSamplers[i];
			static const char *wrapModes = { "REPEAT\0CLAMP_TO_EDGE\0CLAMP_TO_BORDER\0MIRRORED_REPEAT" };
			static const char *filterModes = { "LINEAR\0NEAREST" };
			ImGui::PushItemWidth(150);
			ImGui::Text("Sampler %d", i);
			samplerDirty |= ImGui::Combo("Wrap U", (int*)&inputSampler.mWrapU, wrapModes);
			samplerDirty |= ImGui::Combo("Wrap V", (int*)&inputSampler.mWrapV, wrapModes);
			samplerDirty |= ImGui::Combo("Filter Min", (int*)&inputSampler.mFilterMin, filterModes);
			samplerDirty |= ImGui::Combo("Filter Mag", (int*)&inputSampler.mFilterMag, filterModes);
			ImGui::PopItemWidth();
		}
		if (samplerDirty)
		{
			undoRedoInputSampler.mPostDo = node.mInputSamplers;
			gUndoRedoHandler.AddUndo(undoRedoInputSampler);
			gEvaluation.SetEvaluationSampler(node.gEvaluationTarget, node.mInputSamplers);
		}

	}
	if (!ImGui::CollapsingHeader(currentMeta.mName.c_str(), 0, ImGuiTreeNodeFlags_DefaultOpen))
		return;

	UndoRedoParameterBlock undoRedoParameterBlock(index, node.mParameters);

	unsigned char *paramBuffer = node.mParameters.data();
	int i = 0;
	for(const MetaParameter& param : currentMeta.mParams)
	{
		ImGui::PushID(667889 + i++);
		switch (param.mType)
		{
		case Con_Float:
			dirty |= ImGui::InputFloat(param.mName.c_str(), (float*)paramBuffer);
			break;
		case Con_Float2:
			dirty |= ImGui::InputFloat2(param.mName.c_str(), (float*)paramBuffer);
			break;
		case Con_Float3:
			dirty |= ImGui::InputFloat3(param.mName.c_str(), (float*)paramBuffer);
			break;
		case Con_Float4:
			dirty |= ImGui::InputFloat4(param.mName.c_str(), (float*)paramBuffer);
			break;
		case Con_Color4:
			dirty |= ImGui::ColorPicker4(param.mName.c_str(), (float*)paramBuffer);
			break;
		case Con_Int:
			dirty |= ImGui::InputInt(param.mName.c_str(), (int*)paramBuffer);
			break;
		case Con_Int2:
			dirty |= ImGui::InputInt2(param.mName.c_str(), (int*)paramBuffer);
			break;
		case Con_Ramp:
			{
				//ImVec2 points[8];
					
				RampEdit curveEditDelegate;
				bool allZero = true;
				for (int k = 0; k < 8; k++)
				{
					if (((ImVec4*)paramBuffer)[k].x >= FLT_EPSILON)
					{
						allZero = false;
						break;
					}
				}
				if (allZero)
				{
					((ImVec2*)paramBuffer)[0] = ImVec2(0, 0);
					((ImVec2*)paramBuffer)[1] = ImVec2(1, 1);
					dirty = true;
				}
				curveEditDelegate.mPointCount = 0;
				for (int k = 0; k < 8; k++)
				{
					curveEditDelegate.mPts[k] = ImVec2(((float*)paramBuffer)[k * 2], ((float*)paramBuffer)[k * 2 + 1]);
					if (k && curveEditDelegate.mPts[k-1].x > curveEditDelegate.mPts[k].x)
						break;
					curveEditDelegate.mPointCount++;
				}
				float regionWidth = ImGui::GetWindowContentRegionWidth();
				if (ImCurveEdit::Edit(curveEditDelegate, ImVec2(regionWidth, regionWidth)))
				{
					for (size_t k = 0; k < curveEditDelegate.mPointCount; k++)
					{
						((float*)paramBuffer)[k * 2] = curveEditDelegate.mPts[k].x;
						((float*)paramBuffer)[k * 2 + 1] = curveEditDelegate.mPts[k].y;
					}
					((float*)paramBuffer)[0] = 0.f;
					((float*)paramBuffer)[(curveEditDelegate.mPointCount - 1) * 2] = 1.f;
					for (size_t k = curveEditDelegate.mPointCount; k < 8; k++)
					{
						((float*)paramBuffer)[k * 2] = -1.f;
					}
					dirty = true;
				}
			}
			break;
		case Con_Ramp4:
		{
			float regionWidth = ImGui::GetWindowContentRegionWidth();
			GradientEdit gradientDelegate;

			gradientDelegate.mPointCount = 0;
			bool allZero = true;
			for (int k = 0; k < 8; k++)
			{
				if (((ImVec4*)paramBuffer)[k].w >= FLT_EPSILON)
				{
					allZero = false;
					break;
				}
			}
			if (allZero)
			{
				((ImVec4*)paramBuffer)[0] = ImVec4(0, 0, 0, 0);
				((ImVec4*)paramBuffer)[1] = ImVec4(1, 1, 1, 1);
				dirty = true;
			}
			for (int k = 0; k < 8; k++)
			{
				gradientDelegate.mPts[k] = ((ImVec4*)paramBuffer)[k];
				if (k && gradientDelegate.mPts[k - 1].w > gradientDelegate.mPts[k].w)
					break;
				gradientDelegate.mPointCount++;
			}


			int colorIndex;
			dirty |= ImGradient::Edit(gradientDelegate, ImVec2(regionWidth, 22), colorIndex);
			if (colorIndex != -1)
			{
				dirty |= ImGui::ColorPicker3("", &gradientDelegate.mPts[colorIndex].x);
			}
			if (dirty)
			{
				for (size_t k = 0; k < gradientDelegate.mPointCount; k++)
				{
					((ImVec4*)paramBuffer)[k] = gradientDelegate.mPts[k];
				}
				((ImVec4*)paramBuffer)[0].w = 0.f;
				((ImVec4*)paramBuffer)[gradientDelegate.mPointCount - 1].w = 1.f;
				for (size_t k = gradientDelegate.mPointCount; k < 8; k++)
				{
					((ImVec4*)paramBuffer)[k].w = -1.f;
				}
			}
		}
		break;
		case Con_Angle:
			((float*)paramBuffer)[0] = RadToDeg(((float*)paramBuffer)[0]);
			dirty |= ImGui::InputFloat(param.mName.c_str(), (float*)paramBuffer);
			((float*)paramBuffer)[0] = DegToRad(((float*)paramBuffer)[0]);
			break;
		case Con_Angle2:
			((float*)paramBuffer)[0] = RadToDeg(((float*)paramBuffer)[0]);
			((float*)paramBuffer)[1] = RadToDeg(((float*)paramBuffer)[1]);
			dirty |= ImGui::InputFloat2(param.mName.c_str(), (float*)paramBuffer);
			((float*)paramBuffer)[0] = DegToRad(((float*)paramBuffer)[0]);
			((float*)paramBuffer)[1] = DegToRad(((float*)paramBuffer)[1]);
			break;
		case Con_Angle3:
			((float*)paramBuffer)[0] = RadToDeg(((float*)paramBuffer)[0]);
			((float*)paramBuffer)[1] = RadToDeg(((float*)paramBuffer)[1]);
			((float*)paramBuffer)[2] = RadToDeg(((float*)paramBuffer)[2]);
			dirty |= ImGui::InputFloat3(param.mName.c_str(), (float*)paramBuffer);
			((float*)paramBuffer)[0] = DegToRad(((float*)paramBuffer)[0]);
			((float*)paramBuffer)[1] = DegToRad(((float*)paramBuffer)[1]);
			((float*)paramBuffer)[2] = DegToRad(((float*)paramBuffer)[2]);
			break;
		case Con_Angle4:
			((float*)paramBuffer)[0] = RadToDeg(((float*)paramBuffer)[0]);
			((float*)paramBuffer)[1] = RadToDeg(((float*)paramBuffer)[1]);
			((float*)paramBuffer)[2] = RadToDeg(((float*)paramBuffer)[2]);
			((float*)paramBuffer)[3] = RadToDeg(((float*)paramBuffer)[3]);
			dirty |= ImGui::InputFloat4(param.mName.c_str(), (float*)paramBuffer);
			((float*)paramBuffer)[0] = DegToRad(((float*)paramBuffer)[0]);
			((float*)paramBuffer)[1] = DegToRad(((float*)paramBuffer)[1]);
			((float*)paramBuffer)[2] = DegToRad(((float*)paramBuffer)[2]);
			((float*)paramBuffer)[3] = DegToRad(((float*)paramBuffer)[3]);
			break;
		case Con_FilenameWrite:
		case Con_FilenameRead:
			dirty |= ImGui::InputText("", (char*)paramBuffer, 1024);
			ImGui::SameLine();
			if (ImGui::Button("..."))
			{
				nfdchar_t *outPath = NULL;
				nfdresult_t result = (param.mType == Con_FilenameRead) ? NFD_OpenDialog(NULL, NULL, &outPath) : NFD_SaveDialog(NULL, NULL, &outPath);

				if (result == NFD_OKAY) 
				{
					strcpy((char*)paramBuffer, outPath);
					free(outPath);
					dirty = true;
				}
			}
			ImGui::SameLine();
			ImGui::Text(param.mName.c_str());
			break;
		case Con_Enum:
			dirty |= ImGui::Combo(param.mName.c_str(), (int*)paramBuffer, param.mEnumList);
			break;
		case Con_ForceEvaluate:
			if (ImGui::Button(param.mName.c_str()))
			{
				EvaluationInfo evaluationInfo;
				evaluationInfo.forcedDirty = 1;
				evaluationInfo.uiPass = 0;
				mEditingContext.RunSingle(node.gEvaluationTarget, evaluationInfo);
			}
			break;
		case Con_Bool:
		{
			bool checked = (*(int*)paramBuffer) != 0;
			if (ImGui::Checkbox(param.mName.c_str(), &checked))
			{
				*(int*)paramBuffer = checked ? 1 : 0;
				dirty = true;
			}
		}
		break;
		}
		ImGui::PopID();
		paramBuffer += GetParameterTypeSize(param.mType);
	}
		
	if (dirty)
	{
		undoRedoParameterBlock.mPostDo = node.mParameters;
		gUndoRedoHandler.AddUndo(undoRedoParameterBlock);
		gEvaluation.SetEvaluationParameters(node.gEvaluationTarget, node.mParameters);
		mEditingContext.SetTargetDirty(node.gEvaluationTarget);
	}
}
void TileNodeEditGraphDelegate::SetTimeSlot(size_t index, int frameStart, int frameEnd)
{
	ImogenNode & node = mNodes[index];
	node.mStartFrame = frameStart;
	node.mEndFrame = frameEnd;
}

void TileNodeEditGraphDelegate::SetTimeDuration(size_t index, int duration)
{
	ImogenNode & node = mNodes[index];
	node.mEndFrame = node.mStartFrame + duration;
}

void TileNodeEditGraphDelegate::SetTime(int time, bool updateDecoder)
{
	gEvaluationTime = time;
	for (const ImogenNode& node : mNodes)
	{
		gEvaluation.SetStageLocalTime(node.gEvaluationTarget, ImClamp(time - node.mStartFrame, 0, node.mEndFrame - node.mStartFrame), updateDecoder);
	}
}

size_t TileNodeEditGraphDelegate::ComputeTimelineLength() const
{
	int len = 0;
	for (const ImogenNode& node : mNodes)
	{
		len = ImMax(len, node.mEndFrame);
		len = ImMax(len, int(node.mStartFrame + gEvaluation.GetEvaluationImageDuration(node.gEvaluationTarget)));
	}
	return size_t(len);
}

void TileNodeEditGraphDelegate::DoForce()
{
	int currentTime = gEvaluationTime;
	//gEvaluation.BeginBatch();
	for (ImogenNode& node : mNodes)
	{
		const MetaNode& currentMeta = gMetaNodes[node.mType];
		bool forceEval = false;
		for(auto& param : currentMeta.mParams)
		{
			if (!param.mName.c_str())
				break;
			if (param.mType == Con_ForceEvaluate)
			{
				forceEval = true;
				break;
			}
		}
		if (forceEval)
		{
			EvaluationContext writeContext(gEvaluation, true, 1024, 1024);
			gCurrentContext = &writeContext;
			for (int frame = node.mStartFrame; frame <= node.mEndFrame; frame++)
			{
				SetTime(frame, false);
				EvaluationInfo evaluationInfo;
				evaluationInfo.forcedDirty = 1;
				evaluationInfo.uiPass = 0;
				writeContext.RunSingle(node.gEvaluationTarget, evaluationInfo);
			}
			gCurrentContext = &mEditingContext;
		}
	}
	//gEvaluation.EndBatch();
	SetTime(currentTime, true);
	InvalidateParameters();
}

void TileNodeEditGraphDelegate::InvalidateParameters()
{
	for (auto& node : mNodes)
		gEvaluation.SetEvaluationParameters(node.gEvaluationTarget, node.mParameters);
}

template<typename T> static inline T nmin(T lhs, T rhs) { return lhs >= rhs ? rhs : lhs; }

void TileNodeEditGraphDelegate::SetMouse(float rx, float ry, float dx, float dy, bool lButDown, bool rButDown, float wheel)
{
	if (mSelectedNodeIndex == -1)
		return;

	if (!lButDown)
		mbMouseDragging = false;

	const MetaNode* metaNodes = gMetaNodes.data();
	size_t res = 0;
	const MetaNode& metaNode = metaNodes[mNodes[mSelectedNodeIndex].mType];

	unsigned char *paramBuffer = mNodes[mSelectedNodeIndex].mParameters.data();
	bool parametersUseMouse = false;

	// camera handling
	for (auto& param : metaNode.mParams)
	{
		float *paramFlt = (float*)paramBuffer;
		if (param.mType == Con_Camera)
		{
			Camera *cam = (Camera*)paramBuffer;
			if (cam->mDirection.lengthSq() < FLT_EPSILON)
				cam->mDirection.set(0.f, 0.f, 1.f);
			cam->mPosition += cam->mDirection * wheel;

			parametersUseMouse = true;
		}
		paramBuffer += GetParameterTypeSize(param.mType);
	}

	//
	
	paramBuffer = mNodes[mSelectedNodeIndex].mParameters.data();
	if (lButDown)
	{
		for(auto& param : metaNode.mParams)
		{
			float *paramFlt = (float*)paramBuffer;
			if (param.mType == Con_Camera)
			{
				Camera *cam = (Camera*)paramBuffer;
				if (cam->mDirection.lengthSq() < FLT_EPSILON)
					cam->mDirection.set(0.f, 0.f, 1.f);
				cam->mPosition += cam->mDirection * wheel;
			}
			if (param.mbQuadSelect && param.mType == Con_Float4)
			{
				if (!mbMouseDragging)
				{
					paramFlt[2] = paramFlt[0] = rx;
					paramFlt[3] = paramFlt[1] = 1.f - ry;
					mbMouseDragging = true;
				}
				else
				{
					paramFlt[2] = rx;
					paramFlt[3] = 1.f - ry;
				}
				continue;
			}

			if (param.mRangeMinX != 0.f || param.mRangeMaxX != 0.f)
			{
				if (param.mbRelative)
				{
					paramFlt[0] += ImLerp(param.mRangeMinX, param.mRangeMaxX, dx);
					paramFlt[0] = fmodf(paramFlt[0], fabsf(param.mRangeMaxX - param.mRangeMinX)) + nmin(param.mRangeMinX, param.mRangeMaxX);
				}
				else
				{
					paramFlt[0] = ImLerp(param.mRangeMinX, param.mRangeMaxX, rx);
				}
			}
			if (param.mRangeMinY != 0.f || param.mRangeMaxY != 0.f)
			{
				if (param.mbRelative)
				{
					paramFlt[1] += ImLerp(param.mRangeMinY, param.mRangeMaxY, dy);
					paramFlt[1] = fmodf(paramFlt[1], fabsf(param.mRangeMaxY - param.mRangeMinY)) + nmin(param.mRangeMinY, param.mRangeMaxY);
				}
				else
				{
					paramFlt[1] = ImLerp(param.mRangeMinY, param.mRangeMaxY, ry);
				}
			}
			paramBuffer += GetParameterTypeSize(param.mType);
			parametersUseMouse = true;
		}
	}
	if (metaNode.mbHasUI || parametersUseMouse)
	{
		gEvaluation.SetMouse(mSelectedNodeIndex, rx, ry, lButDown, rButDown);
		gEvaluation.SetEvaluationParameters(mNodes[mSelectedNodeIndex].gEvaluationTarget, mNodes[mSelectedNodeIndex].mParameters);
		mEditingContext.SetTargetDirty(mNodes[mSelectedNodeIndex].gEvaluationTarget);
	}
}

size_t TileNodeEditGraphDelegate::ComputeNodeParametersSize(size_t nodeTypeIndex)
{
	size_t res = 0;
	for(auto& param : gMetaNodes[nodeTypeIndex].mParams)
	{
		res += GetParameterTypeSize(param.mType);
	}
	return res;
}
bool TileNodeEditGraphDelegate::NodeIsCubemap(size_t nodeIndex)
{
	RenderTarget *target = mEditingContext.GetRenderTarget(nodeIndex);
	if (target)
		return target->mImage.mNumFaces == 6;
	return false;
}

ImVec2 TileNodeEditGraphDelegate::GetEvaluationSize(size_t nodeIndex)
{
	int imageWidth(1), imageHeight(1);
	gEvaluation.GetEvaluationSize(int(nodeIndex), &imageWidth, &imageHeight);
	return ImVec2(float(imageWidth), float(imageHeight));
}
