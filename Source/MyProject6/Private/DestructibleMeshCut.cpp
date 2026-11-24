#include "DestructibleMeshCut.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

namespace
{
	bool IsInsideSphereCutter(const FDynamicMesh3& Mesh, const FTransform& ToWorld,
		const FVector& Center, double Radius)
	{
		// Both gameplay cutters have at least 12 latitude and 24 longitude samples.
		// The inner 90% is entirely inside their planar facets. Test actual vertices
		// so render bounds overrides and nonuniform scale cannot erase a partial hit.
		const double InnerRadiusSquared = FMath::Square(Radius * .9);
		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			if (!(FVector::DistSquared(ToWorld.TransformPosition(Mesh.GetVertex(Vertex)), Center) < InnerRadiusSquared))
			{
				return false;
			}
		}
		return true;
	}
}

void DestructibleMesh::SubtractSphere(UDynamicMeshComponent& Component, const FVector& Center,
	double Radius, TFunctionRef<UDynamicMesh*()> GetSphereMesh)
{
	UDynamicMesh* Target = Component.GetDynamicMesh();
	if (!Target || Target->IsEmpty()) { return; }

	const bool bHadCollision = Component.GetCollisionEnabled() != ECollisionEnabled::NoCollision;
	const FTransform ToWorld = Component.GetComponentTransform();
	Component.SetTransientDeferCollisionUpdates(true);
	if (IsInsideSphereCutter(Target->GetMeshRef(), ToWorld, Center, Radius))
	{
		FDynamicMesh3 Empty;
		Empty.EnableMatchingAttributes(Target->GetMeshRef());
		Target->SetMesh(MoveTemp(Empty));
	}
	else
	{
		FGeometryScriptMeshBooleanOptions Options;
		Options.bFillHoles = true;
		Options.bSimplifyOutput = true;
		Options.bAllowEmptyResult = true;
		Options.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Target, ToWorld, GetSphereMesh(),
			FTransform(Center), EGeometryScriptBooleanOperation::Subtract, Options);
		FGeometryScriptSplitNormalsOptions Split;
		Split.bSplitByOpeningAngle = true;
		Split.OpeningAngleDeg = 60.f;
		UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(Target, Split, FGeometryScriptCalculateNormalsOptions());
	}
	// SetMesh and the normals edit already update bounds, tangents and the scene proxy.
	Component.SetTransientDeferCollisionUpdates(false);
	Component.SetComplexAsSimpleCollisionEnabled(bHadCollision, true);
}
