import boto3
from boto3.dynamodb.conditions import Key, Attr
from botocore.exceptions import ClientError

import json
import time
import logging
import os
from io import StringIO, BytesIO

# Pandas and matplotlib need to be installed in a layer for the Lambda function
# to be able to use them; for instructions see e.g.
# https://towardsdatascience.com/python-packages-in-aws-lambda-made-easy-8fbc78520e30
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.dates import DateFormatter
from pytz import timezone

dynamodb_table = '<CONFIGURE>'
out_bucket = '<CONFIGURE>'

def upload_file(file_name, bucket, object_name=None):
    """Upload a file to an S3 bucket

    :param file_name: File to upload
    :param bucket: Bucket to upload to
    :param object_name: S3 object name. If not specified then file_name is used
    :return: True if file was uploaded, else False
    """

    # If S3 object_name was not specified, use file_name
    if object_name is None:
        object_name = os.path.basename(file_name)

    # Upload the file
    s3_client = boto3.client('s3')
    try:
        response = s3_client.upload_file(file_name, bucket, object_name,
                    ExtraArgs={'ACL': 'public-read'}) # Only if plots should be publicly accessible
    except ClientError as e:
        logging.error(e)
        return False
    return True

def generate_plot(data, filename, timeframe="48h"):
    """Generate a plot of the temperature and humidity

    :param data: Pandas DataFrame containing temperature, humidity against
    a DateTimeIndex
    :param filename: Output filename
    :param timeframe: Filter only the most recent data based on this timeframe
    """

    # Get only the requested timeframe
    df = data.last(timeframe)

    fig, ax = plt.subplots()

    # Temperature plot
    ax.plot(df["temperature_smooth"])
    ax.set_ylabel('Temperature (C)', color="tab:blue")
    ax.tick_params(axis='y', labelcolor="tab:blue")

    # Humidity plot in the same image
    ax2 = ax.twinx()
    ax2.plot(df["humidity_smooth"], color="tab:orange")
    ax2.set_ylabel('Humidity (%)', color="tab:orange")
    ax2.tick_params(axis='y', labelcolor="tab:orange")

    # Set time format on the x-axis
    x_format = DateFormatter("%d - %H:%M")
    x_format.set_tzinfo(timezone('Europe/Amsterdam'))
    ax.xaxis.set_major_formatter(x_format)
    fig.autofmt_xdate()

    # Store output image
    plt.savefig(filename, format="png", dpi=200)

def lambda_handler(event, context):
    dynamodb = boto3.resource('dynamodb')
    table = dynamodb.Table(dynamodb_table)
    smoothing_window = "20min"

    # Pull only the last two days worth of data since we never plot any more
    epoch_time = int(time.time())
    cutoff_time = epoch_time - 2*86400

    response = table.scan(
        FilterExpression = \
            Attr('timestamp').gte(cutoff_time) & \
            Attr('timestamp').lt(epoch_time)
        )

    # Convert to Pandas DataFrame
    df = pd.json_normalize(response["Items"]).astype(
        {"timestamp": int,
        "temperature": float,
        "humidity": float}).dropna(subset=["timestamp"]).sort_values("timestamp")

    # Add DateTimeIndex
    df["readable_timestamp"] = pd.to_datetime(df["timestamp"], unit='s')
    df = df.set_index("readable_timestamp")

    # Smooth data to make the plot more readable
    df["temperature_smooth"] = df["temperature"].rolling(smoothing_window).mean()
    df["humidity_smooth"] = df["humidity"].rolling(smoothing_window).mean()

    # Generate plots
    generate_plot(df, "/tmp/48h.png", "48h")
    generate_plot(df, "/tmp/12h.png", "12h")
    generate_plot(df, "/tmp/2h.png", "2h")
    upload_file("/tmp/48h.png", out_bucket)
    upload_file("/tmp/12h.png", out_bucket)
    upload_file("/tmp/2h.png", out_bucket)

    return "DONE"
