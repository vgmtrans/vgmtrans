/*
 * fluid_defsfont_load
 */
int fluid_defsfont_load(fluid_defsfont_t* sfont, const char* file)
{
    SFData* sfdata;
    fluid_list_t *p;
    SFPreset* sfpreset;
    SFSample* sfsample;
    fluid_sample_t* sample;
    fluid_defpreset_t* preset;

    sfont->filename = FLUID_MALLOC(1 + FLUID_STRLEN(file));
    if (sfont->filename == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return FLUID_FAILED;
    }
    FLUID_STRCPY(sfont->filename, file);

    /* The actual loading is done in the sfont and sffile files */
    sfdata = sfload_file(file);
    if (sfdata == NULL) {
        FLUID_LOG(FLUID_ERR, "Couldn't load soundfont file");
        return FLUID_FAILED;
    }

    /* Keep track of the position and size of the sample data because
       it's loaded separately (and might be unoaded/reloaded in future) */
    sfont->samplepos = sfdata->samplepos;
    sfont->samplesize = sfdata->samplesize;

    /* load sample data in one block */
    if (fluid_defsfont_load_sampledata(sfont) != FLUID_OK)
        goto err_exit;

    /* Create all the sample headers */
    p = sfdata->sample;
    while (p != NULL) {
        sfsample = (SFSample *) p->data;

        sample = new_fluid_sample();
        if (sample == NULL) goto err_exit;

        if (fluid_sample_import_sfont(sample, sfsample, sfont) != FLUID_OK)
            goto err_exit;

        /* Store reference to FluidSynth sample in SFSample for later IZone fixups */
        sfsample->fluid_sample = sample;

        fluid_defsfont_add_sample(sfont, sample);
        fluid_voice_optimize_sample(sample);
        p = fluid_list_next(p);
    }

    /* Load all the presets */
    p = sfdata->preset;
    while (p != NULL) {
        sfpreset = (SFPreset *) p->data;
        preset = new_fluid_defpreset(sfont);
        if (preset == NULL) goto err_exit;

        if (fluid_defpreset_import_sfont(preset, sfpreset, sfont) != FLUID_OK)
            goto err_exit;

        fluid_defsfont_add_preset(sfont, preset);
        p = fluid_list_next(p);
    }
    sfont_close (sfdata);

    return FLUID_OK;

    err_exit:
    sfont_close (sfdata);
    return FLUID_FAILED;
}