index=0;
for name in *.png
do
  mv "${name}" `printf "%04d" $index`.png
  index=$((index+1))
done
