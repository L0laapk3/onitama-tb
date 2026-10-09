#!/usr/bin/env bash
# IMPORTANT: THESE COMMANDS ARE ONLY TO BE RUN BY THE HUMAN AS THEY COST MONEY TO RUN. CLAUDE OR OTHER AI's SHOULD NOT RUN THESE COMMANDS UNDER ANY CIRCUMSTANCES.
set -euo pipefail

# I could not get quota for more than 96 cores, on europe_west

# gcloud beta compute instance-templates create tb \
#   --machine-type=c4d-standard-96 \
#   --instance-template-region=europe-west2 \
#   --max-run-duration=300s \
#   --provisioning-model=SPOT --preemption-notice-duration=120s --instance-termination-action=DELETE \
#   --boot-disk-size=10GB --boot-disk-type=hyperdisk-balanced \
#   --network-interface=nic-type=GVNIC,network-tier=STANDARD

# gcloud config:
# gcloud auth login --no-launch-browser
# gcloud config set compute/zone europe-west2-b
# gcloud config set compute/region europe-west2

bazel build //tb --config=znver5

# The braces make bash parse the whole script before running, so editing it mid-run is safe.
{
echo "[$(date +%T)] Creating VM tb-run"
gcloud compute instances create tb-run --source-instance-template="$(gcloud compute instance-templates describe tb --region=europe-west2 --format='value(selfLink)')"

# Waits for sshd and pushes your gcloud SSH key to the VM.
echo "[$(date +%T)] Waiting for SSH to come up"
until gcloud compute ssh tb-run --command=true 2>/dev/null; do sleep 3; done

echo "[$(date +%T)] Uploading bazel-bin/tb/tb"
gcloud compute scp bazel-bin/tb/tb tb-run:tb

echo "[$(date +%T)] Running ./tb 10 draw10perf.bin"
gcloud compute ssh tb-run --command='./tb 10 draw10perf.bin'

echo "[$(date +%T)] Downloading draw10perf.bin"
gcloud compute scp tb-run:draw10perf.bin .

echo "[$(date +%T)] Deleting VM tb-run"
gcloud compute instances delete tb-run --quiet

echo "[$(date +%T)] Done"
exit
}
