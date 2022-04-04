SELECT
    newuuid() as uuid,
    timestamp,
    temperature,
    humidity,
    timestamp+172800 as ttl -- 48h time-to-live for DynamoDB TTL function
FROM '<CONFIGURE>'          -- IoT Core topic (AWS_IOT_TOPIC on ESP32)
