#Inclusion guards
for i in `find . -type f \( -iname \*.H -o -iname \*.cpp -o -iname \*.py \)`; do
    sed -i 's/\#include \"perlin_rod_if.H\"/\#include <CD_PerlinRodSdf.H>/g' $i
    sed -i 's/\#include <perlin_rod_if.H>/\#include <CD_PerlinRodSdf.H>/g' $i
done

for i in `find . -type f \( -iname \*.H -o -iname \*.cpp -o -iname \*.py -o -iname \*.inputs -o -iname \*.options -o -iname *GNUmakefile \)`; do
    sed -i 's/perlin_rod_if/PerlinRodSdf/g' $i
done

# Move files

mv Source/Geometry/perlin_rod_if.H   Source/Geometry/CD_PerlinRodSdf.H
mv Source/Geometry/perlin_rod_if.cpp Source/Geometry/CD_PerlinRodSdf.cpp
