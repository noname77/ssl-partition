TARGET_ADDRESS=172.31.36.248
BATCH_SIZE=5000
CONCURRENT_LEVEL=10

SIZE_MIN=1
SIZE_MAX=4096
SIZE_INC=2
SIZE_SUFFIX=K

OUTPUT_DIR=$(pwd)/out

if [ ! -d "$OUTPUT_DIR" ]; then
  mkdir -p $OUTPUT_DIR
fi

cd $OUTPUT_DIR

rm -f *.out

COUNTER=$SIZE_MIN

while [  $COUNTER -le $SIZE_MAX ]; do
  echo  
  echo Running "$COUNTER""$SIZE_SUFFIX"
  ab -c $CONCURRENT_LEVEL -n $BATCH_SIZE -Z AES256-GCM-SHA384 https://$TARGET_ADDRESS/"$COUNTER""$SIZE_SUFFIX".bin | tee "$COUNTER""$SIZE_SUFFIX"_RSA.out
  ab -c $CONCURRENT_LEVEL -n $BATCH_SIZE -Z ECDHE-RSA-AES256-GCM-SHA384 https://$TARGET_ADDRESS/"$COUNTER""$SIZE_SUFFIX".bin | tee "$COUNTER""$SIZE_SUFFIX"_ECDHE.out
  let COUNTER=COUNTER*SIZE_INC
done
