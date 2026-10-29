#include "ParametricGroundGeometry.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
using namespace UE::Geometry;
void ParametricGroundGeometry::BuildSlab(FDynamicMesh3& Mesh, EParametricGroundShape Shape,
    double SafeSize, double SafeThickness, double SafeUV, int32 CircleSegments,
    bool bShowBoundary, float BoundaryInset, float BoundaryWidth)
{
	const int32 Sides = Shape == EParametricGroundShape::Square ? 4 :
		Shape == EParametricGroundShape::Hexagon ? 6 : FMath::Clamp(CircleSegments, 16, 512);
	const double Step = UE_DOUBLE_TWO_PI / Sides;
	const double CornerCos = FMath::Cos(Step * 0.5);
	const double StartAngle = Shape == EParametricGroundShape::Square ? UE_DOUBLE_PI * 0.25 : 0.0;
	// Polygon offsets use the apothem, so square/hexagon corners preserve the same perpendicular gap.
	const double Apothem = SafeSize * 0.5 * (Shape == EParametricGroundShape::Circle ? CornerCos : 1.0);
	const double MinimumInterior = FMath::Max(0.01, Apothem * 1.e-6);
	const double Inset = FMath::Clamp(FMath::IsFinite(BoundaryInset) ? FMath::Max(double(BoundaryInset), 0.0) : 500.0,
		0.0, Apothem - 2.0 * MinimumInterior);
	const double Width = bShowBoundary ? FMath::Clamp(
		FMath::IsFinite(BoundaryWidth) ? FMath::Max(double(BoundaryWidth), 0.0) : 100.0, 0.0, Apothem - Inset - MinimumInterior) : 0.0;

	Mesh.EnableAttributes();
	Mesh.Attributes()->EnableMaterialID();
	Mesh.Attributes()->SetNumUVLayers(1);
	auto* Normals = Mesh.Attributes()->PrimaryNormals();
	auto* UVs = Mesh.Attributes()->PrimaryUV();

	auto AddTriangle = [&](int32 A, int32 B, int32 C, int32 MaterialID, bool bSide = false)
	{
		// Unreal renders clockwise front faces; the input loops use outward CCW normals.
		const int32 Triangle = Mesh.AppendTriangle(A, C, B);
		check(Triangle >= 0);
		Mesh.Attributes()->GetMaterialID()->SetValue(Triangle, MaterialID);
		const FVector3f FaceNormal(Mesh.GetTriNormal(Triangle));
		FIndex3i NormalIDs, UVIDs;
		const FIndex3i Vertices(A, C, B);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector3d P = Mesh.GetVertex(Vertices[Corner]);
			const FVector3f Normal = bSide && Shape == EParametricGroundShape::Circle
				? FVector3f(FVector3d(P.X, P.Y, 0.0).GetSafeNormal()) : FaceNormal;
			NormalIDs[Corner] = Normals->AppendElement(Normal);
			const FVector2f UV = bSide
				? FVector2f((P.X * -FaceNormal.Y + P.Y * FaceNormal.X) / SafeUV, P.Z / SafeUV)
				: FVector2f(P.X / SafeUV, P.Y / SafeUV);
			UVIDs[Corner] = UVs->AppendElement(UV);
		}
		Normals->SetTriangle(Triangle, NormalIDs);
		UVs->SetTriangle(Triangle, UVIDs);
	};
	auto AddRing = [&](double RingApothem, double Z)
	{
		TArray<int32> Ring;
		Ring.Reserve(Sides);
		const double Radius = RingApothem / CornerCos;
		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const double Angle = StartAngle + Step * Index;
			Ring.Add(Mesh.AppendVertex(FVector3d(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), Z)));
		}
		return Ring;
	};
	auto ConnectTopRings = [&](const TArray<int32>& Outer, const TArray<int32>& Inner, int32 MaterialID)
	{
		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const int32 Next = (Index + 1) % Sides;
			AddTriangle(Outer[Index], Outer[Next], Inner[Next], MaterialID);
			AddTriangle(Outer[Index], Inner[Next], Inner[Index], MaterialID);
		}
	};

	const TArray<int32> Top = AddRing(Apothem, 0.0);
	const TArray<int32> Bottom = AddRing(Apothem, -SafeThickness);
	TArray<int32> InnerTop = Top;
	if (Width > UE_DOUBLE_SMALL_NUMBER)
	{
		if (Inset > UE_DOUBLE_SMALL_NUMBER)
		{
			InnerTop = AddRing(Apothem - Inset, 0.0);
			ConnectTopRings(Top, InnerTop, 0);
		}
		TArray<int32> BoundaryInner = AddRing(Apothem - Inset - Width, 0.0);
		ConnectTopRings(InnerTop, BoundaryInner, 1);
		InnerTop = MoveTemp(BoundaryInner);
	}
	const int32 TopCenter = Mesh.AppendVertex(FVector3d::Zero());
	const int32 BottomCenter = Mesh.AppendVertex(FVector3d(0.0, 0.0, -SafeThickness));
	for (int32 Index = 0; Index < Sides; ++Index)
	{
		const int32 Next = (Index + 1) % Sides;
		AddTriangle(TopCenter, InnerTop[Index], InnerTop[Next], 0);
		AddTriangle(BottomCenter, Bottom[Next], Bottom[Index], 0);
		AddTriangle(Top[Index], Bottom[Index], Bottom[Next], 0, true);
		AddTriangle(Top[Index], Bottom[Next], Top[Next], 0, true);
	}

}
