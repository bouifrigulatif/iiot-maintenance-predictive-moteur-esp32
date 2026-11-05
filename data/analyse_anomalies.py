import pandas as pd

FICHIER = "mesures.csv"

# 1. Lecture du fichier
df = pd.read_csv(FICHIER)

# 2. Nettoyage : on ignore les lignes sans valeur valide
df["temperature"] = pd.to_numeric(df["temperature"], errors="coerce")
df["vibration"] = pd.to_numeric(df["vibration"], errors="coerce")
df = df.dropna(subset=["temperature", "vibration"]).reset_index(drop=True)

# 3. Conversion du temps (secondes Unix -> date lisible)
df["heure"] = pd.to_datetime(df["ts"], unit="s")

FENETRE = 10   # nombre de mesures pour la moyenne mobile
K = 3.0        # largeur de la bande : K ecarts-types

# 4. Moyenne mobile et ecart-type sur les 10 mesures PRECEDENTES
temp = df["temperature"]
df["moy"] = temp.rolling(FENETRE).mean().shift(1)
df["std"] = temp.rolling(FENETRE).std().shift(1)

# 5. Ecart-type minimal pour ne pas signaler du bruit infime
ecart = df["std"].clip(lower=0.05)

# 6. Seuil adaptatif : moyenne +/- K ecarts-types
df["haut"] = df["moy"] + K * ecart
df["bas"] = df["moy"] - K * ecart

# 7. Anomalie : la mesure sort de la bande
df["anomalie"] = (temp > df["haut"]) | (temp < df["bas"])

import matplotlib.pyplot as plt

# 8. Export des anomalies
anomalies = df[df["anomalie"]]
anomalies[["ts", "heure", "temperature", "vibration", "statut"]].to_csv(
    "anomalies.csv", index=False)
print(len(df), "mesures analysees,", len(anomalies), "anomalie(s) detectee(s)")

# 9. Graphique
plt.figure(figsize=(10, 5))
plt.plot(df["heure"], temp, label="temperature", color="tab:blue")
plt.plot(df["heure"], df["moy"], label="moyenne mobile", color="tab:green")
plt.fill_between(df["heure"], df["bas"], df["haut"], alpha=0.2,
                 color="tab:green", label="seuil adaptatif (3 sigma)")
plt.scatter(anomalies["heure"], anomalies["temperature"],
            color="red", zorder=5, label="anomalie")
plt.ylabel("Temperature (°C)")
plt.legend()
plt.tight_layout()
plt.savefig("anomalies.png", dpi=150)
print("Graphique enregistre : anomalies.png")