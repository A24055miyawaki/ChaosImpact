// Game-only FBX copy. Original mesh/UVs/limb weights are preserved; add a shared body rig.
// Build with the FBX SDK shipped with UE 5.7 (FBXSDK_SHARED).
#include <fbxsdk.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

FbxNode* Joint(FbxScene* Scene, FbxNode* Parent, const char* Name, FbxVector4 World)
{
    auto* N = FbxNode::Create(Scene,Name);
    auto* S = FbxSkeleton::Create(Scene,Name);
    S->SetSkeletonType(Parent == Scene->GetRootNode() ? FbxSkeleton::eRoot : FbxSkeleton::eLimbNode);
    N->SetNodeAttribute(S);
    Parent->AddChild(N);
    N->LclTranslation.Set(Parent->EvaluateGlobalTransform().Inverse().MultT(World));
    return N;
}

void ParentKeepingBind(FbxNode* Node, FbxNode* Parent)
{
    const FbxAMatrix World = Node->EvaluateGlobalTransform();
    Parent->AddChild(Node);
    const FbxAMatrix Local = Parent->EvaluateGlobalTransform().Inverse() * World;
    Node->SetPreRotation(FbxNode::eSourcePivot,FbxVector4(0,0,0));
    Node->SetPostRotation(FbxNode::eSourcePivot,FbxVector4(0,0,0));
    Node->LclTranslation.Set(Local.GetT());
    Node->LclRotation.Set(Local.GetR());
    Node->LclScaling.Set(Local.GetS());
}

int main(int argc,char** argv)
{
    try {
        if (argc != 3) throw std::runtime_error("Usage: prepare_fairy source.fbx game-copy.fbx");
        auto* Manager = FbxManager::Create();
        Manager->SetIOSettings(FbxIOSettings::Create(Manager,IOSROOT));
        auto* Scene = FbxScene::Create(Manager,"DodgeFairy");
        auto* Importer = FbxImporter::Create(Manager,"");
        if (!Importer->Initialize(argv[1],-1,Manager->GetIOSettings()) || !Importer->Import(Scene))
            throw std::runtime_error(Importer->GetStatus().GetErrorString());
        Importer->Destroy();
        auto* MeshNode = Scene->FindNodeByName("pSphere2");
        if (!MeshNode || !MeshNode->GetMesh()) throw std::runtime_error("Fairy mesh not found");
        auto* Mesh = MeshNode->GetMesh();
        auto* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0,FbxDeformer::eSkin));
        if (!Skin) throw std::runtime_error("Skin not found");
        // Preserve pointers before renaming: the two leg chains use duplicate joint8/9/10 names.
        auto* ArmR = Scene->FindNodeByName("Migiude");
        auto* ArmL = Scene->FindNodeByName("Hidariude");
        auto* LegR = Scene->FindNodeByName("Migiasi");
        auto* LegL = Scene->FindNodeByName("Hidariasi");
        if (!ArmR || !ArmL || !LegR || !LegL) throw std::runtime_error("Limb root missing");
        auto RenameChain = [](FbxNode* N,const char* Side,bool Arm) {
            const char* PartsArm[] = {"Arm","ForeArm","Hand"};
            const char* PartsLeg[] = {"UpLeg","Leg","Foot","ToeBase"};
            for (int i=0;i<(Arm?3:4);++i) {
                if (!N) throw std::runtime_error("Incomplete limb chain");
                N->SetName((std::string("Character1_")+Side+(Arm?PartsArm[i]:PartsLeg[i])).c_str());
                FbxNode* Next = nullptr;
                for (int j=0;j<N->GetChildCount();++j) if (N->GetChild(j)->GetSkeleton()) { Next=N->GetChild(j); break; }
                N=Next;
            }
        };
        RenameChain(ArmR,"Right",true); RenameChain(ArmL,"Left",true);
        RenameChain(LegR,"Right",false); RenameChain(LegL,"Left",false);
        auto* Root = Joint(Scene,Scene->GetRootNode(),"Character1_Reference",FbxVector4(0,0,0));
        auto* Hips = Joint(Scene,Root,"Character1_Hips",FbxVector4(0,99.895,0));
        auto* Spine = Joint(Scene,Hips,"Character1_Spine",FbxVector4(0,106,0));
        auto* Spine1 = Joint(Scene,Spine,"Character1_Spine1",FbxVector4(0,111,0));
        auto* Spine2 = Joint(Scene,Spine1,"Character1_Spine2",FbxVector4(0,116,0));
        auto* Neck = Joint(Scene,Spine2,"Character1_Neck",FbxVector4(0,121,0));
        auto* Head = Joint(Scene,Neck,"Character1_Head",FbxVector4(0,125,0));
        auto* ShoulderL = Joint(Scene,Spine2,"Character1_LeftShoulder",ArmL->EvaluateGlobalTransform().GetT());
        auto* ShoulderR = Joint(Scene,Spine2,"Character1_RightShoulder",ArmR->EvaluateGlobalTransform().GetT());
        ParentKeepingBind(ArmL,ShoulderL); ParentKeepingBind(ArmR,ShoulderR);
        ParentKeepingBind(LegL,Hips); ParentKeepingBind(LegR,Hips);

        // Identify disconnected mesh islands. A round body was accidentally weighted to the left hip:
        // move only the central, above-hip islands to the new head, never hand/arm islands.
        const int Count=Mesh->GetControlPointsCount();
        std::vector<int> Groups(Count);
        for (int i=0;i<Count;++i) Groups[i]=i;
        auto Find = [&Groups](int i) { while (Groups[i]!=i) i=Groups[i]; return i; };
        for (int p=0;p<Mesh->GetPolygonCount();++p) {
            const int First=Mesh->GetPolygonVertex(p,0);
            for (int v=1;v<Mesh->GetPolygonSize(p);++v) Groups[Find(Mesh->GetPolygonVertex(p,v))]=Find(First);
        }
        std::map<int,std::vector<int>> Islands;
        for (int i=0;i<Count;++i) Islands[Find(i)].push_back(i);
        std::set<int> BodyVertices;
        for (auto& Pair:Islands) {
            double MinY=1e9,MaxY=-1e9,MinX=1e9,MaxX=-1e9;
            for (int v:Pair.second) {
                const auto P=Mesh->GetControlPointAt(v);
                MinY=std::min(MinY,P[1]); MaxY=std::max(MaxY,P[1]);
                MinX=std::min(MinX,P[0]); MaxX=std::max(MaxX,P[0]);
            }
            const bool Body=MinY>85 && MaxY>130 && MinX>-30 && MaxX<30;
            std::cout<<"Island verts="<<Pair.second.size()<<" x="<<MinX<<","<<MaxX<<" y="<<MinY<<","<<MaxY<<" body="<<Body<<"\n";
            if (Body) BodyVertices.insert(Pair.second.begin(),Pair.second.end());
        }
        if (BodyVertices.empty()) throw std::runtime_error("No isolated body geometry; inspect before modifying weights");
        for (int c=0;c<Skin->GetClusterCount();++c) {
            auto* Cluster=Skin->GetCluster(c);
            auto* Indices=Cluster->GetControlPointIndices(); auto* Weights=Cluster->GetControlPointWeights();
            for (int i=0;i<Cluster->GetControlPointIndicesCount();++i) if (BodyVertices.count(Indices[i])) Weights[i]=0;
            Cluster->SetTransformLinkMatrix(Cluster->GetLink()->EvaluateGlobalTransform());
        }
        auto* BodyCluster=FbxCluster::Create(Scene,"FairyBody");
        BodyCluster->SetLink(Head); BodyCluster->SetLinkMode(FbxCluster::eNormalize);
        BodyCluster->SetTransformMatrix(MeshNode->EvaluateGlobalTransform());
        BodyCluster->SetTransformLinkMatrix(Head->EvaluateGlobalTransform());
        for (int v:BodyVertices) BodyCluster->AddControlPointIndex(v,1.0);
        Skin->AddCluster(BodyCluster);
        // Replace stale exporter bind poses after reparenting; all parents must be included.
        while (Scene->GetPoseCount()) { auto* P=Scene->GetPose(0); Scene->RemovePose(0); P->Destroy(); }
        auto* Bind=FbxPose::Create(Scene,"GameBindPose"); Bind->SetIsBindPose(true);
        std::set<FbxNode*> Included;
        auto Add = [&Included,Bind](FbxNode* N) {
            for (;N && N->GetParent();N=N->GetParent()) if (Included.insert(N).second) Bind->Add(N,FbxMatrix(N->EvaluateGlobalTransform()));
        };
        Add(MeshNode);
        for (int c=0;c<Skin->GetClusterCount();++c) Add(Skin->GetCluster(c)->GetLink());
        Scene->AddPose(Bind);
        auto* Exporter=FbxExporter::Create(Manager,"");
        if (!Exporter->Initialize(argv[2],-1,Manager->GetIOSettings()) || !Exporter->Export(Scene))
            throw std::runtime_error(Exporter->GetStatus().GetErrorString());
        std::cout<<"Prepared "<<argv[2]<<", body vertices="<<BodyVertices.size()<<"\n";
        Exporter->Destroy(); Manager->Destroy(); return 0;
    } catch (const std::exception& E) { std::cerr<<E.what()<<"\n"; return 1; }
}
