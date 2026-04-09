import csv
import argparse
import os
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description="Plot any two columns from summary.csv")
parser.add_argument("--file", default="results/summary.csv", help="CSV filename")
parser.add_argument("--x", required=True, help="Column name for x-axis")
parser.add_argument("--y", required=True, help="Column name for y-axis")
parser.add_argument("--title", default=None, help="Custom plot title")
args = parser.parse_args()

filename = args.file
x_col = args.x
y_col = args.y

data = {
    "westwood": {"x": [], "y": []},
    "imtcpbg": {"x": [], "y": []},
    "imtcpbgimproved": {"x": [], "y": []}
}

with open(filename, "r") as f:
    reader = csv.DictReader(f)

    if x_col not in reader.fieldnames:
        raise ValueError(f"Column '{x_col}' not found in CSV. Available: {reader.fieldnames}")
    if y_col not in reader.fieldnames:
        raise ValueError(f"Column '{y_col}' not found in CSV. Available: {reader.fieldnames}")

    for row in reader:
        tcp = row["bgTcp"]

        try:
            x_val = float(row[x_col])
            y_val = float(row[y_col])
        except ValueError:
            continue

        if tcp in data:
            data[tcp]["x"].append(x_val)
            data[tcp]["y"].append(y_val)

plt.figure(figsize=(9, 6))

styles = {
    "westwood": {"marker": "o", "linestyle": "-",  "linewidth": 2.5},
    "imtcpbg": {"marker": "s", "linestyle": "--", "linewidth": 2.5},
    "imtcpbgimproved": {"marker": "^", "linestyle": "-.", "linewidth": 2.5}
}

for tcp in data:
    paired = sorted(zip(data[tcp]["x"], data[tcp]["y"]))
    x = [p[0] for p in paired]
    y = [p[1] for p in paired]

    plt.plot(
        x, y,
        label=tcp,
        marker=styles[tcp]["marker"],
        linestyle=styles[tcp]["linestyle"],
        linewidth=styles[tcp]["linewidth"],
        markersize=8
    )

plt.xlabel(x_col)
plt.ylabel(y_col)

if args.title:
    plt.title(args.title)
else:
    plt.title(f"{y_col} vs {x_col}")

plt.legend()
plt.grid(True)
plt.tight_layout()

os.makedirs("results/plots", exist_ok=True)
os.makedirs(f"results/plots/Wireless/{x_col}", exist_ok=True)
output_file = f"results/plots/Wireless/{x_col}/{y_col}_vs_{x_col}.png"

plt.savefig(output_file, dpi=300)
print(f"Saved plot to {output_file}")
plt.show()

# ! bar graph 

# import csv
# import os
# import sys
# import matplotlib.pyplot as plt

# filename = "results/summary.csv"

# # Check command-line argument
# if len(sys.argv) != 2:
#     print(f"Usage: python3 {sys.argv[0]} <y_column>")
#     sys.exit(1)

# y_column = sys.argv[1]

# data = []

# with open(filename, "r") as f:
#     reader = csv.DictReader(f)

#     # Validate column name
#     if y_column not in reader.fieldnames:
#         print(f"Error: Column '{y_column}' not found in {filename}")
#         print("Available columns:")
#         for col in reader.fieldnames:
#             print(f"  {col}")
#         sys.exit(1)

#     for row in reader:
#         try:
#             data.append((row["bgTcp"], float(row[y_column])))
#         except ValueError:
#             print(f"Skipping invalid value for {y_column} in row: {row}")

# x_vals = [item[0] for item in data]
# y_vals = [item[1] for item in data]

# output_dir = "plots/Main/bgTcp"
# os.makedirs(output_dir, exist_ok=True)

# plt.figure(figsize=(8, 5))
# plt.bar(x_vals, y_vals)
# plt.xlabel("bgTcp")
# plt.ylabel(y_column)
# plt.title(f"{y_column} vs bgTcp")
# plt.tight_layout()

# output_file = os.path.join(output_dir, f"{y_column}_vs_bgTcp.png")
# plt.savefig(output_file, dpi=300)
# plt.show()

# print(f"Plot saved to: {output_file}")